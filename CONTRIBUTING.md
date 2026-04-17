# Contributing to NEXRAD Radar Level II Processing Pipeline

First off, thank you for considering contributing! It's people like you that make the open-source community such an amazing place to learn, inspire, and create.

## How Can I Contribute?

### Reporting Bugs

Before creating bug reports, please check the existing issues as you might find out that you don't need to create one. When you are creating a bug report, please include as many details as possible:

*   **Use a clear and descriptive title** for the issue to identify the problem.
*   **Describe the exact steps which reproduce the problem** in as many details as possible.
*   **Describe the behavior you observed after following the steps** and point out what exactly is the problem with that behavior.
*   **Explain which behavior you expected to see instead and why.**
*   **Include screenshots** if possible.

### Suggesting Enhancements

If you have an idea for a new feature or an enhancement, please open an issue and:

*   **Use a clear and descriptive title.**
*   **Provide a step-by-step description of the suggested enhancement** in as many details as possible.
*   **Provide specific examples to demonstrate the steps.**
*   **Describe the current behavior and explain which behavior you expected to see instead** and why.

### Your First Code Contribution

#### Local Development Setup

1.  **Prerequisites**: Ensure you have a C++17 compatible compiler, CMake 3.15+, and the required dependencies (CURL, BZip2, ZLIB, AWS SDK for C++, nlohmann_json).
2.  **Building**: Follow the instructions in [docs/BUILD.md](./docs/BUILD.md).
3.  **Running**: Follow the instructions in [docs/RUNNING.md](./docs/RUNNING.md).

#### Code Style

*   We use C++17 features.
*   Follow the existing coding style in the project.
*   Use descriptive variable and function names.
*   Add comments to complex logic.

#### Testing

*   Always test new features and bug fixes
*   Run tests using:
    ```bash
    mkdir -p build && cd build
    cmake -DENABLE_TESTING=ON ..
    make
    ctest
    ```

### Pull Requests

1.  Fork the repo and create your branch from `main`.
2.  If you've added code that should be tested, add tests.
3.  If you've changed APIs, update the documentation.
4.  Ensure the test suite passes.
5.  Make sure your code lints.
6.  Issue that Pull Request!

## Community

By participating in this project, you agree to abide by our [Code of Conduct](./CODE_OF_CONDUCT.md).
