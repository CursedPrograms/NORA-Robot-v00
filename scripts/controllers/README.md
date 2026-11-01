# NORA's controllers in other languages

Python and the Windows exe are NORA's main controllers. These are the same small controller in five languages, all reading `robot.json`, which holds NORA's commands, buttons and sensors. Change it there and they all change.

Hold W / S to go forward and back, A / D to strafe, Q / E to turn. Space or STOP stops it in any mode. These have no keys, click them in the window: MANUAL, AUTO, LINE, FOLLOW, STAY CLOSE, HELLO, DANCE, LOCK. Pass the address as the first argument, or type it in.

| Language | UI | Run |
|---|---|---|
| C# | WinForms | `dotnet run --project csharp` |
| F# | WinForms | `dotnet run --project fsharp` |
| Rust | egui | `cargo run --release --manifest-path rust/Cargo.toml` |
| Go | Ebitengine | `cd go && go run .` (Go builds its own copy of `robot.json` in, so change both) |
| Julia | GLMakie | `julia julia/controller.jl` |
