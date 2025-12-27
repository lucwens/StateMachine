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
