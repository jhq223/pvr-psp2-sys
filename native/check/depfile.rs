//! Normalize SNC's quoted Windows dependencies for CMake/Ninja.
use std::collections::BTreeSet;

fn escape(path: &str) -> String {
    path.replace('\\', "/")
        .replace('$', "$$")
        .replace('#', "\\#")
        .replace(' ', "\\ ")
}

pub fn normalize(source: &str, object: &str) -> Result<String, String> {
    let mut dependencies = BTreeSet::new();
    for line in source.lines().filter(|line| !line.trim().is_empty()) {
        let (_, dependency) = line
            .split_once(": ")
            .ok_or_else(|| format!("invalid SNC dependency: {line}"))?;
        let dependency = dependency.trim();
        let dependency = if dependency.starts_with('"') {
            dependency
                .strip_prefix('"')
                .and_then(|s| s.strip_suffix('"'))
                .ok_or("unterminated dependency path")?
        } else {
            dependency
        };
        if dependency.is_empty() {
            return Err("empty dependency path".into());
        }
        dependencies.insert(escape(dependency));
    }
    if dependencies.is_empty() {
        return Err("empty SNC dependency file".into());
    }
    Ok(format!(
        "{}: {}\n",
        escape(object),
        dependencies.into_iter().collect::<Vec<_>>().join(" ")
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handles_windows_paths_spaces_and_make_metacharacters() {
        let input = "foo.o: foo.c\nfoo.o: \"D:\\SDK Software\\a#b$.h\"\nfoo.o: foo.c\n";
        assert_eq!(
            normalize(input, "D:/build/foo.o").unwrap(),
            "D:/build/foo.o: D:/SDK\\ Software/a\\#b$$.h foo.c\n"
        );
        assert!(normalize("", "o").is_err());
        assert!(normalize("o: \"unclosed", "o").is_err());
    }
}
