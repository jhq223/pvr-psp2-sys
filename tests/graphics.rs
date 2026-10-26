#![cfg(all(feature = "graphics", not(target_os = "vita")))]

use pvr_psp2_sys::graphics::{
    AttributeBinding, Blend, Context, Draw, ShaderCode, Target, TextureFormat, UniformValue,
    VertexAttribute,
};
use std::time::{Duration, Instant};

#[test]
#[ignore = "requires a host EGL ES2 implementation; submits commands without readback or a window"]
fn headless_upload_draw_fence_and_lifetime() {
    assert!(Context::headless([0, 544], false).is_err());
    for size in [[960, 544], [320, 180]] {
        let context = Context::headless(size, false).unwrap();
        assert_eq!(context.output_size(), size);
        println!(
            "driver={:?} capabilities={:?}",
            context.driver_info().unwrap(),
            context.capabilities()
        );
        assert!(
            Context::headless(size, false).is_err(),
            "native ownership is exclusive"
        );
        context.set_vsync(true).unwrap();
        context.set_vsync(false).unwrap();
        let texture = context
            .create_texture([2, 2], TextureFormat::Rgba8)
            .unwrap();
        context
            .upload(&texture, [0, 0], [2, 2], &[255; 16], 8)
            .unwrap();
        if context.capabilities().bc {
            for (format, bytes) in [(TextureFormat::Bc1Rgb, 32), (TextureFormat::Bc3Rgba, 64)] {
                let blocks = vec![0; bytes];
                let mut staging = vec![0xa5; bytes];
                let compressed = context
                    .create_bc_texture([8, 8], format, &blocks, &mut staging)
                    .unwrap();
                assert_eq!(
                    staging,
                    vec![0xa5; bytes],
                    "desktop uploads standard blocks directly"
                );
                drop(compressed);
            }
        }
        let program = context.create_program(
            ShaderCode::Source("#version 100\nattribute vec2 pos; varying vec2 uv; void main() { uv=pos*.5+.5; gl_Position=vec4(pos,0.,1.); }"),
            ShaderCode::Source("#version 100\nprecision mediump float; varying vec2 uv; uniform sampler2D image; void main() { gl_FragColor=texture2D(image,uv); }"),
            &[AttributeBinding { location: 0, name: "pos" }],
        ).unwrap();
        let sampler = program.uniform("image").unwrap();
        let mut vertices = context
            .vertex_buffer(
                &[-1., -1., 1., -1., 1., 1., -1., -1., 1., 1., -1., 1.],
                2,
                &[VertexAttribute {
                    location: 0,
                    components: 2,
                    offset: 0,
                }],
            )
            .unwrap();
        let start = Instant::now();
        for _ in 0..120 {
            context.clear(Target::Screen, [0., 0., 0., 1.]).unwrap();
            context
                .draw(Draw {
                    target: Target::Screen,
                    size,
                    program: &program,
                    vertices: &vertices,
                    indices: None,
                    range: 0..6,
                    textures: &[&texture],
                    uniforms: &[UniformValue::Int(&sampler, 0)],
                    blend: Blend::Replace,
                    scissor: None,
                })
                .unwrap();
            context.present().unwrap();
        }
        let fence = context.fence().unwrap();
        context.flush().unwrap();
        assert!(
            fence.wait(Duration::from_secs(5)).unwrap(),
            "submitted GPU work must complete"
        );
        assert!(fence.poll().unwrap());
        let mut indices = context.index_buffer(&[0, 1, 2, 3, 4, 5]).unwrap();
        context
            .update_vertices(&mut vertices, &[-1., -1., 1., -1., 1., 1.])
            .unwrap();
        context.update_indices(&mut indices, &[0, 1, 2]).unwrap();
        assert!(context.update_vertices(&mut vertices, &[0.; 14]).is_err());
        assert!(context.update_vertices(&mut vertices, &[0.; 3]).is_err());
        assert!(context.update_indices(&mut indices, &[0; 7]).is_err());
        context
            .draw(Draw {
                target: Target::Screen,
                size,
                program: &program,
                vertices: &vertices,
                indices: Some(&indices),
                range: 0..3,
                textures: &[&texture],
                uniforms: &[UniformValue::Int(&sampler, 0)],
                blend: Blend::Replace,
                scissor: None,
            })
            .unwrap();
        // The active index extent shrinks with the update, despite retained capacity.
        assert!(
            context
                .draw(Draw {
                    target: Target::Screen,
                    size,
                    program: &program,
                    vertices: &vertices,
                    indices: Some(&indices),
                    range: 0..6,
                    textures: &[&texture],
                    uniforms: &[UniformValue::Int(&sampler, 0)],
                    blend: Blend::Replace,
                    scissor: None,
                })
                .is_err()
        );
        let updated = context.fence().unwrap();
        context.flush().unwrap();
        assert!(updated.wait(Duration::from_secs(5)).unwrap());
        drop((updated, indices));
        println!(
            "pbuffer={size:?} completed_frames=120 elapsed_us={}",
            start.elapsed().as_micros()
        );
        drop((vertices, sampler, program, texture));
        let (context, _) = context.close().expect_err("fence keeps the context alive");
        drop(fence);
        context
            .close()
            .unwrap_or_else(|(_, error)| panic!("close: {error}"));
    }
}
