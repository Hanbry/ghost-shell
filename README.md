# Ghost (in the) Shell

A C shell implementation with an integrated AI assistant that helps you accomplish tasks on your machine. The "ghost" in the shell is an AI that understands your intent and helps you achieve it through commands.

## 🧠 The Ghost - AI Assistant

The shell's standout feature is its integrated AI that can:
- Understand natural language requests and convert them to shell commands
- It will work on the command line as you would expect from a human

Example interactions:
```bash
# Ask the ghost to perform a task
call create a backup of my documents folder and compress it
```

## Quick Start

Build and run:
```bash
make
./bin/ghost-shell        # Start as interactive non-login shell
./bin/ghost-shell -l     # Start as login shell
# or
./bin/ghost-shell --login
```

The only requirement is an OpenAI API key:
```bash
export OPENAI_API_KEY='your-api-key'
```

## Shell Startup Files

The shell uses two startup files:

1. `~/.ghsh_profile`: Sourced only for login shells
   - Use this for environment variables, PATH settings
   - Typically used for machine-wide configuration

2. `~/.ghshrc`: Sourced for all interactive shells (including login shells)
   - Use this for shell customizations (aliases, functions, prompt settings)
   - Sourced after .ghsh_profile for login shells

Login shells will source both files, while non-login interactive shells will only source `~/.ghshrc`.

## Standard Shell Features

Also includes all standard shell features:
- Command execution with environment variables
- I/O redirection (`<`, `>`, `>>`) and pipelines (`|`)
- Background processes (`&`) and here-docs (`<<`)
- Command history (stored in ~/.ghsh_history) and tab completion
- Custom prompt and line editing

## Dependencies

On Linux/BSD systems, you'll need to install dependencies first:
```bash
# Ubuntu/Debian
sudo apt-get install libedit-dev
```

## License

MIT licensed. See LICENSE file. 