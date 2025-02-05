# Ghost Shell

A lightweight and extensible shell implementation written in C, licensed under MIT.

## Features

- Basic command execution
- Built-in commands (`cd`, `exit`, `help`)
- Input/output redirection (`<`, `>`)
- Background process execution (`&`)
- Error handling and reporting
- Memory management
- Clean and modular code structure
- Tab completion and command history

## Dependencies

The shell requires a line editing library. By default, it uses libedit (BSD-licensed):
- On macOS: Already installed (uses system libedit)
- On Linux/BSD: Install libedit
  ```bash
  # Ubuntu/Debian
  sudo apt-get install libedit-dev
  
  # Fedora
  sudo dnf install libedit-devel
  
  # FreeBSD
  sudo pkg install libedit
  ```

## Building

The project uses a simple Makefile build system:

```bash
# Build release version (using libedit - BSD licensed)
make

# Build debug version (using libedit)
make debug

# Clean build artifacts
make clean

# Optional: Build with GNU readline (creates GPL dependency)
make readline
```

### License Considerations

- Default build uses libedit (BSD-licensed) and is MIT-compatible
- Optional readline build creates GPL dependency due to GNU readline
- If you build with `make readline`, the resulting binary must be distributed under GPL

## Project Structure

- `src/` - Source files
  - `main.c` - Entry point and shell initialization
  - `shell.c` - Core shell loop and utilities
  - `command.c` - Command parsing and execution
  - `builtins.c` - Built-in command implementations
- `include/` - Header files
  - `ghost_shell.h` - Main header with structures and function declarations
- `build/` - Object files (created during build)
- `bin/` - Binary output directory

## Usage

After building, run the shell:

```bash
./bin/ghost-shell
```

### Basic Commands

```bash
# Change directory
cd [directory]

# Get help
help

# Exit the shell
exit [status]

# Run a command in background
command &

# Input/Output redirection
command < input.txt > output.txt
```

## Development

The shell is designed to be extensible. New features can be added by:

1. Adding new command structures in `ghost_shell.h`
2. Implementing command parsing in `command.c`
3. Adding command execution logic in `shell.c`
4. Implementing new built-in commands in `builtins.c`

## License

This project is licensed under the terms specified in the LICENSE file. 