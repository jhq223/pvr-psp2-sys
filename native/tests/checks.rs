#[path = "../check/abi.rs"]
mod abi;

fn u16_at(data: &mut [u8], offset: usize, value: u16) {
    data[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}
fn u32_at(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn object(name: &str, nid: u32) -> Vec<u8> {
    let names = format!("\0{name}\0");
    let mut data = vec![0; 204 + names.len()];
    data[..7].copy_from_slice(b"\x7fELF\x01\x01\x01");
    u16_at(&mut data, 16, 1);
    u16_at(&mut data, 18, 40);
    u32_at(&mut data, 32, 52);
    u16_at(&mut data, 46, 40);
    u16_at(&mut data, 48, 3);
    u32_at(&mut data, 96, 2); // SHT_SYMTAB
    u32_at(&mut data, 108, 172);
    u32_at(&mut data, 112, 32);
    u32_at(&mut data, 116, 2); // sh_link to strings
    u32_at(&mut data, 128, 16);
    u32_at(&mut data, 136, 3); // SHT_STRTAB
    u32_at(&mut data, 148, 204);
    u32_at(&mut data, 152, names.len() as u32);
    u32_at(&mut data, 188, 1); // name
    u32_at(&mut data, 192, nid);
    u16_at(&mut data, 202, 0xFFF1);
    data[204..].copy_from_slice(names.as_bytes());
    data
}

fn archive(objects: &[Vec<u8>]) -> Vec<u8> {
    let mut data = b"!<arch>\n".to_vec();
    for object in objects {
        let mut header = [b' '; 60];
        header[..6].copy_from_slice(b"obj.o/");
        header[48..58].copy_from_slice(format!("{:<10}", object.len()).as_bytes());
        header[58..].copy_from_slice(b"`\n");
        data.extend_from_slice(&header);
        data.extend_from_slice(object);
        if object.len() & 1 != 0 {
            data.push(b'\n');
        }
    }
    data
}

const MANIFEST: &str = "# expected\n_NID_glDraw 0x12345678\n";

#[test]
fn unchanged_exports_and_additional_exports_are_accepted() {
    let data = archive(&[object("_NID_glDraw", 0x12345678), object("_NID_new", 1)]);
    assert_eq!(abi::verify(MANIFEST, &data), Ok(1));
}

#[test]
fn changed_missing_and_conflicting_exports_fail() {
    assert!(abi::verify(MANIFEST, &archive(&[object("_NID_glDraw", 0)])).is_err());
    assert!(abi::verify(MANIFEST, &archive(&[object("_NID_other", 0x12345678)])).is_err());
    assert!(
        abi::verify(
            MANIFEST,
            &archive(&[object("_NID_glDraw", 0x12345678), object("_NID_glDraw", 0)])
        )
        .is_err()
    );
}

#[test]
fn malformed_archive_lengths_headers_and_elf_tables_fail_without_panicking() {
    let original = archive(&[object("_NID_glDraw", 0x12345678)]);
    for length in 0..original.len() {
        assert!(
            abi::verify(MANIFEST, &original[..length]).is_err(),
            "length {length}"
        );
    }
    for (offset, value) in [(48, b'x'), (58, b'x')] {
        let mut bad = original.clone();
        bad[8 + offset] = value;
        assert!(abi::verify(MANIFEST, &bad).is_err());
    }
    for (offset, value) in [
        (4, 2),
        (5, 2),
        (18, 0),
        (46, 0),
        (48, 0),
        (116, 255),
        (128, 0),
        (188, 255),
    ] {
        let mut bad = object("_NID_glDraw", 0x12345678);
        bad[offset] = value;
        assert!(
            abi::verify(MANIFEST, &archive(&[bad])).is_err(),
            "offset {offset}"
        );
    }
}

#[test]
fn manifest_must_be_nonempty_and_unambiguous() {
    let data = archive(&[object("_NID_glDraw", 0x12345678)]);
    for manifest in [
        "",
        "# only comment",
        "glDraw 0x12345678",
        "_NID_glDraw xyz",
        "_NID_glDraw 0",
        "_NID_glDraw 0x12345678\n_NID_glDraw 0x00",
    ] {
        assert!(abi::verify(manifest, &data).is_err());
    }
}
