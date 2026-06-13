# Atlas

# File Structure
```
atlas/
├── CMakeLists.txt          # Root CMake configuration
├── include/
│   └── atlas/              # Public API / Header files
│       ├── core/           # Core lifecycle and event dispatching interfaces
│       └── storage/        # Storage layer interfaces 
├── src/
│   ├── core/               # Core implementation files
│   ├── storage/            # Filesystem-based persistence implementation
│   └── main.cpp            # Application entry point
├── tests/                  # Unit tests directory
└── third_party/            # Directory for external dependencies (if needed later)

Design Decision: Separating include/ and src/ enforces a strict boundary between public interfaces and private implementations. This is crucial since Atlas is an extensible AI workspace platform, and this structure ensures future tools or UI layers only depend on what is explicitly exposed in the include/ directory.
