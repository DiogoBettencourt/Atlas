# Atlas Project: Architecture & Development Scope

## 1. Project Vision
Atlas is a local-first, extensible AI workspace built primarily in modern C++20. It is designed not just as a chatbot, but as a robust platform for interacting with language models, managing projects, analyzing repositories, executing tools, and running AI agents—all while maintaining complete user privacy and data ownership.

## 2. Core Principles
* **Local-First Architecture:** AI processing and data storage happen locally (e.g., via Ollama).
* **Modern C++20:** Emphasis on safety, performance, and modern language features.
* **Extensibility:** The system uses a plugin-like Strategy pattern for tools, agents, and storage interfaces.
* **Separation of Concerns:** Strict division between public interfaces (`include/atlas/`) and private implementations (`src/`).
* **Minimal Dependencies:** Relying heavily on standard libraries, `nlohmann/json`, `asio`, and lightweight header-only libraries (like `cpp-httplib`).
* **Human-Readable Storage:** Workspaces and chat histories are saved as beautifully formatted JSON files.

## 3. High-Level Architecture
Atlas operates as a headless AI microservice. The frontend (Qt, React, CLI) communicates with the C++ backend over an HTTP REST API.

### Core Modules
1. **Core / Application (`Application.hpp`):** The central nervous system. Bootstraps the configuration, manages the asynchronous event loop (`asio::io_context`), and orchestrates dependency injection.
2. **API Server (`APIServer.hpp`):** Uses `cpp-httplib` to expose endpoints (e.g., `POST /chat`). It decouples the AI logic from the user interface.
3. **Workspace Manager (`WorkspaceManager.hpp`):** Context-aware router. Resolves dynamic paths based on the active project instead of relying on brittle Current Working Directory (CWD) logic.
4. **Storage Layer (`StorageManager.hpp`):** Abstract interfaces managing filesystem-based JSON persistence. Fully decouples logic from disk I/O.
5. **Tool Registry (`ToolManager.hpp`):** A dynamic registry for executable tools. Tools provide LLM-compatible JSON Schemas describing their capabilities.
6. **Agent Runtime (`Agent.hpp`):** The reasoning engine executing a ReAct (Reason + Act) loop. Handles tool call interception, stateful chat history (via `SessionManager`), and fallback parsing for LLM JSON leaks.
7. **Symbol Indexer (`SymbolIndexer.hpp`):** Parses the workspace codebase to build a graph of classes, methods, and files. Enables the "Librarian Architecture" for low-VRAM environments.

## 4. Coding Standards & C++ Guidelines
When writing or planning code for Atlas, agents and developers must strictly adhere to these rules:

* **Use Modern C++20 Idioms:** Utilize concepts, ranges, and `std::filesystem`.
* **Resource Management:** Consistently use RAII. Never use raw owning pointers. Always prefer `std::unique_ptr` for composition and `std::shared_ptr` only when shared ownership is strictly required.
* **Struct/Data Mapping:** Use `nlohmann/json` non-intrusive macros (e.g., `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`) to map C++ structs to JSON seamlessly.
* **Const Correctness:** Apply `const` extensively to getters, parameters, and variables.
* **No Premature Optimization:** Optimize for clean separation of concerns and maintainability first.
* **Error Handling:** Avoid silent failures. Use exceptions for exceptional states, or `std::optional` for expected missing data (e.g., when a file might not exist).
* **Class Design:** Delete copy and move constructors on singleton-like core managers (`Application`, `WorkspaceManager`).

## 5. Agentic AI & Model Considerations (The Librarian Architecture)
Atlas is designed to be built and run on hardware with limited VRAM (e.g., 6GB limits). Therefore, agents interacting with Atlas must **not** attempt to load the entire repository into context. 

* **Targeted Retrieval:** Use the `SearchSymbolTool` to find the exact file and line number of a class or function.
* **Surgical Edits:** Use `ReadFileTool` and proposed `EditFileTool` to read/modify only specific chunks of code. Keep context usage under 4,000 tokens.
* **ReAct Loop Execution:** 
    1. Parse user request.
    2. Search codebase graph for relevant context.
    3. Read specific files.
    4. Propose code edits.
    5. Run compiler checks and self-correct using compiler logs.
* **Strict Tool Usage:** LLM output should strictly adhere to JSON schemas for tool calls. Do not leak conversational markdown into tool JSON boundaries.