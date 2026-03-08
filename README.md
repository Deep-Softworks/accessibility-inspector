# accessibility-inspector

`axtrace` is a Zig CLI for viewing the macOS Accessibility tree of a running app.

## Build

```sh
zig build
```

## Run

```sh
zig build run -- Safari
zig build run -- Notes
zig build run -- Slack
```

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
