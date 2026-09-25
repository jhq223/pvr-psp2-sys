//! Inspect ARM ELF import archives using only the Rust standard library.
use std::{collections::BTreeMap, str};

type Result<T> = std::result::Result<T, String>;

fn bytes(data: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    let end = offset.checked_add(length).ok_or("binary range overflow")?;
    data.get(offset..end)
        .ok_or_else(|| format!("truncated binary at {offset}+{length}"))
}

fn u16_at(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(
        bytes(data, offset, 2)?.try_into().unwrap(),
    ))
}

fn u32_at(data: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(
        bytes(data, offset, 4)?.try_into().unwrap(),
    ))
}

fn insert(nids: &mut BTreeMap<String, u32>, name: &str, value: u32) -> Result<()> {
    if let Some(old) = nids.insert(name.to_owned(), value)
        && old != value
    {
        return Err(format!(
            "conflicting NIDs for {name}: 0x{old:08X}, 0x{value:08X}"
        ));
    }
    Ok(())
}

fn elf_nids(data: &[u8], nids: &mut BTreeMap<String, u32>) -> Result<()> {
    let header = bytes(data, 0, 52)?;
    if header[4..7] != [1, 1, 1] || u16_at(header, 16)? != 1 || u16_at(header, 18)? != 40 {
        return Err("expected a little-endian ELF32 ARM relocatable stub".into());
    }
    let start = u32_at(header, 32)? as usize;
    let stride = usize::from(u16_at(header, 46)?);
    let count = usize::from(u16_at(header, 48)?);
    if stride < 40 || count == 0 {
        return Err("invalid ELF section table".into());
    }
    let table = bytes(data, start, stride * count)?;
    let section = |index: usize| -> Result<&[u8]> {
        if index >= count {
            return Err(format!("invalid ELF section index {index}"));
        }
        bytes(table, index * stride, 40)
    };
    let section_data = |s: &[u8]| bytes(data, u32_at(s, 16)? as usize, u32_at(s, 20)? as usize);
    for index in 0..count {
        let s = section(index)?;
        if u32_at(s, 4)? != 2 {
            // SHT_SYMTAB
            continue;
        }
        let strings_section = section(u32_at(s, 24)? as usize)?;
        if u32_at(strings_section, 4)? != 3 {
            // SHT_STRTAB
            return Err("ELF symbol table does not link to a string table".into());
        }
        let strings = section_data(strings_section)?;
        let symbols = section_data(s)?;
        let entry_size = u32_at(s, 36)? as usize;
        if entry_size < 16 || symbols.len() % entry_size != 0 {
            return Err("invalid ELF symbol stride".into());
        }
        for symbol in symbols.chunks_exact(entry_size) {
            if u16_at(symbol, 14)? != 0xFFF1 {
                // SHN_ABS
                continue;
            }
            let offset = u32_at(symbol, 0)? as usize;
            let tail = strings
                .get(offset..)
                .ok_or("invalid ELF symbol name offset")?;
            let end = tail
                .iter()
                .position(|byte| *byte == 0)
                .ok_or("unterminated ELF symbol name")?;
            let name = str::from_utf8(&tail[..end]).map_err(|_| "invalid ELF symbol name")?;
            if name.starts_with("_NID_") {
                insert(nids, name, u32_at(symbol, 4)?)?;
            }
        }
    }
    Ok(())
}

fn archive_nids(data: &[u8]) -> Result<BTreeMap<String, u32>> {
    if !data.starts_with(b"!<arch>\n") {
        return Err("not an ar import archive".into());
    }
    let mut position = 8;
    let mut nids = BTreeMap::new();
    while position < data.len() {
        let header = bytes(data, position, 60)?;
        if &header[58..60] != b"`\n" {
            return Err("invalid ar member header".into());
        }
        let size = str::from_utf8(&header[48..58])
            .map_err(|_| "invalid ar member size")?
            .trim()
            .parse::<usize>()
            .map_err(|_| "invalid ar member size")?;
        position += 60;
        let member = bytes(data, position, size)?;
        if member.starts_with(b"\x7fELF") {
            elf_nids(member, &mut nids)?;
        }
        // The ar format pads odd-length members to an even boundary.
        position += size;
        bytes(data, position, size & 1)?;
        position += size & 1;
    }
    if nids.is_empty() {
        return Err("no exported NIDs in archive".into());
    }
    Ok(nids)
}

/// Manifest rows are `<symbol> <hex NID>`; extra exports are allowed, but
/// every original symbol must remain present with exactly the same ID.
pub fn verify(manifest: &str, archive: &[u8]) -> Result<usize> {
    let mut expected = BTreeMap::new();
    for (line_number, line) in manifest.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields.len() != 2 || !fields[0].starts_with("_NID_") {
            return Err(format!("invalid NID manifest row {}", line_number + 1));
        }
        let value = u32::from_str_radix(fields[1].trim_start_matches("0x"), 16)
            .map_err(|_| format!("invalid NID for {}", fields[0]))?;
        insert(&mut expected, fields[0], value)?;
    }
    if expected.is_empty() {
        return Err("empty NID manifest".into());
    }
    let actual = archive_nids(archive)?;
    for (name, value) in &expected {
        match actual.get(name) {
            Some(found) if found == value => {}
            Some(found) => {
                return Err(format!(
                    "{name}: expected 0x{value:08X}, found 0x{found:08X}"
                ));
            }
            None => return Err(format!("missing export {name} (0x{value:08X})")),
        }
    }
    Ok(expected.len())
}
