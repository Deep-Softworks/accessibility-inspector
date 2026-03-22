# accessibility-inspector

`axtrace` is a CLI for viewing the macOS Accessibility tree of a running app.

## Build

```sh
make
```

## Run

```sh
./axtrace Safari
./axtrace --depth 5 Finder
./axtrace --focus-only Notes
./axtrace --show-values --show-geometry Slack
./axtrace --json --show-actions Safari
./axtrace --role button Finder
./axtrace --search Save Notes
./axtrace --count Safari
./axtrace --list-apps
```

## Flags

- `--depth <n>`: maximum traversal depth (default `20`)
- `--focus-only`: trace from the focused AX element instead of the application root
- `--show-values`: include `value="..."` lines for nodes that expose `AXValue`
- `--show-geometry`: show position and size of each element
- `--show-actions`: list available accessibility actions per element
- `--show-all-attrs`: dump all attribute names per element
- `--role <pattern>`: filter tree to nodes whose role contains the pattern (case-insensitive)
- `--search <text>`: show only subtrees with values containing the text (case-insensitive)
- `--json`: output as JSON instead of tree diagram
- `--count`: show element type statistics instead of the tree
- `--list-apps`: list running macOS applications

## Example Output

```text
AXApplication
 └─ AXWindow
     └─ AXScrollArea
         └─ AXTextArea
             value="Hello world"
             selectedRange={12,0}
```

## Permissions

If Accessibility access has not been granted, `axtrace` prints:

```text
Accessibility permission required.
Enable it in:
System Settings → Privacy & Security → Accessibility
```
