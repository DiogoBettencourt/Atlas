# Atlas: Extensible AI Workspace Platform

Atlas is a **local-first, privacy-focused** AI workspace platform developed in modern **C++20**. Designed as an extensible framework rather than a monolithic chatbot, Atlas provides a self-hosted environment for managing workspaces, executing tools, running AI agents, and performing knowledge management — all while ensuring users maintain full ownership of their data.

---

## Core Philosophy

Atlas is built on a foundation of clean architectural boundaries and long-term maintainability:

- **Local-First & Privacy-Focused** — Designed for secure, self-hosted deployment on local hardware.
- **Extensible & Modular** — Core responsibilities (application lifecycle, storage access, and tool execution) are abstracted behind interfaces, allowing for seamless scaling and integration of new components without requiring system-wide rewrites.
- **Modern Standards** — Developed using C++20, prioritizing SOLID principles, RAII, and const correctness.
- **Maintainability** — Long-term project health and clean separation of concerns over short-term hacks.

---

## Technical Stack

Atlas leverages modern C++ tools to provide high performance with minimal dependencies:

| Component              | Technology                                      |
|------------------------|-------------------------------------------------|
| **Language**           | C++20                                           |
| **Build System**       | CMake (utilizing FetchContent for dependencies) |
| **Networking / Async** | Boost.Asio or standalone Asio                   |
| **Data Serialization** | nlohmann/json                                   |
| **AI Integration**     | Ollama and OpenAI-compatible APIs (extensible for future providers such as llama.cpp) |
| **Storage**            | JSON files and filesystem-based persistence (no external database required for the initial implementation) |

---

## Architectural Strategy

To ensure clarity and manage complexity as the project grows, Atlas employs several key design strategies:

- **Modular Architecture** — The system is partitioned into independent components, allowing individual services — such as LLM providers or tool sets — to scale independently of the core system.
- **Separation of Concerns** — A strict layout (`include/` for public interfaces, `src/` for private implementations) maintains a clear boundary that keeps the core system stable while new layers (such as UI or API servers) are developed.
- **Agent Runtime** — An extensible workflow for agent-based tasks:

  ```
  Task → Plan → Tool Execution → Observation → Reflection → Result
  ```

## Getting Started

### Prerequisites

- C++20 compatible compiler
- CMake 3.x+

### Building Atlas

Atlas uses CMake's **FetchContent** to simplify dependency management — required libraries are downloaded automatically during configuration.

```bash
# Clone the repository
git clone <repository-url>
cd atlas

# Create a build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Run
./atlas
```

---

> Built as a modern, extensible AI workspace environment.
