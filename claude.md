# Claude Code Guidelines for StateMachine Project

## Code Formatting

All C++ code in this project MUST follow the formatting rules defined in `.clang-format`.

Key formatting rules (Microsoft-based style):

- **Brace Style**: Allman/Microsoft - opening braces go on their own line
- **Column Limit**: 160 characters
- **Indent Case Labels**: Yes
- **Namespace Indentation**: All
- **Short Functions**: Only inline functions can be on a single line

### Example of Correct Formatting

```cpp
struct Example
{
    void doSomething()
    {
        if (condition)
        {
            // code
        }
        else
        {
            // code
        }
    }
};
```

### Incorrect (Do NOT use)

```cpp
struct Example {
    void doSomething() {
        if (condition) {
            // code
        } else {
            // code
        }
    }
};
```

## When Writing Code

1. Always use Allman-style braces (opening brace on new line)
2. When adding code examples to documentation (README.md, etc.), use the same formatting style
3. Run clang-format before committing if unsure about formatting

## Markdown Formatting Rules

When writing or editing markdown files (README.md, etc.):

1. **Tables require a blank line before them** - Tables will not render correctly without a blank line separating them from the preceding content (especially after bold text or headers)

   ```markdown
   <!-- CORRECT -->
   **My Header:**

   | Column 1 | Column 2 |
   |----------|----------|
   | data     | data     |

   <!-- INCORRECT - table won't render properly -->
   **My Header:**
   | Column 1 | Column 2 |
   |----------|----------|
   | data     | data     |
   ```

2. **Use consistent table alignment** - Align the pipe characters for readability in source

## README.md Maintenance

**IMPORTANT**: The README.md file MUST be kept in sync with the codebase. Update README.md when making ANY of the following changes:

### Must Update README.md When:

1. **Adding/Removing/Renaming Events or Commands**
   - Update the "Events vs Commands" tables
   - Update the "State Transition Table"
   - Update the Mermaid state diagrams if transitions change

2. **Changing the API**
   - Update the "Usage Example" code snippet
   - Update the "Architecture" class diagram
   - Update the "C++ Programming Patterns Used" section if patterns change

3. **Adding/Removing States**
   - Update the "State Hierarchy" Mermaid diagram
   - Update the "State Transition Table"
   - Update the "Architecture" class diagram

4. **Changing Interactive Mode Commands**
   - Update the "Interactive Mode Commands" tables

5. **Adding/Removing Demo Scenarios**
   - Update the "Demo Scenarios" list

6. **Changing Build Configuration**
   - Update "Building the Project" section
   - Update "Project Structure" if files are added/removed

7. **Changing Design Decisions**
   - Update "Key Design Decisions" section

### README.md Sections to Check:

| Change Type | Sections to Update |
|-------------|-------------------|
| Events/Commands | Events vs Commands, State Transition Table, State Hierarchy diagram |
| API changes | Usage Example, Architecture diagram, C++ Patterns |
| States | State Hierarchy, State Transition Table, Architecture |
| Interactive commands | Interactive Mode Commands |
| File structure | Project Structure |
| Dependencies | Requirements, Key Design Decisions |

## Architecture Principles

### Message Design (Events vs Commands)

This project uses semantic distinction between Events and Commands:

- **Events** (namespace `Events::`) - Past tense, "what happened"
  - Examples: `InitComplete`, `TargetFound`, `ErrorOccurred`
  - The system reacts to these external occurrences

- **Commands** (namespace `Commands::`) - Imperative, "what to do"
  - Examples: `PowerOn`, `StartSearch`, `Reset`
  - Instructions that drive the state machine

### Flat Variant Design

All message types are in a single flat `StateMessage` variant:
```cpp
using StateMessage = std::variant<
    Events::InitComplete, Events::InitFailed, ...
    Commands::PowerOn, Commands::PowerOff, ...
>;
```

This design ensures:
- No nested variants
- No if/else dispatch logic needed
- Uniform `operator()()` on all types for name extraction
- Single `processMessage()` function handles everything

### Uniform API

The API is intentionally uniform:
- `sendMessage()` - works for both Events and Commands
- `sendMessageAsync()` - works for both Events and Commands
- `processMessage()` - single entry point for HSM

The namespace distinction (`Events::` vs `Commands::`) provides semantic clarity while processing remains uniform.

## Testing

When adding new functionality:
1. Add unit tests in `tests/` directory
2. Test files follow pattern: `test_*.cpp`
3. Use Google Test framework
4. Test both HSM core (`test_hsm.cpp`) and ThreadedHSM (`test_threaded_hsm.cpp`)

## Commit Messages

Follow conventional commit format:
- `feat:` for new features
- `fix:` for bug fixes
- `refactor:` for code restructuring
- `docs:` for documentation changes
- `test:` for test additions/changes
