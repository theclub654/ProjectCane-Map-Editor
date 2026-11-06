"""Local mailbox transport and confined, read-only source knowledge."""
from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile
import time
import uuid


class EditorConnection:
    def __init__(self, bridge: str | None = None, timeout: float = 15):
        self.bridge = Path(bridge).resolve() if bridge else None
        self.timeout = timeout

    def discover(self) -> tuple[Path, dict]:
        candidates = [self.bridge] if self.bridge else list(
            (Path(tempfile.gettempdir()) / "projectcane-mcp").glob("*"))
        live = []
        for folder in candidates:
            try:
                marker = folder / "session.json"
                if folder.is_symlink() or marker.is_symlink() or time.time() - marker.stat().st_mtime > 12:
                    continue
                data = json.loads(marker.read_text(encoding="utf-8"))
                if data.get("protocol") == 1 and data.get("session") == folder.name:
                    live.append((folder, data))
            except (OSError, ValueError):
                continue
        if len(live) != 1:
            raise RuntimeError("Enable Help > AI / MCP Connection in one editor, or pass --bridge with its folder. "
                               f"Found {len(live)} live sessions.")
        return live[0]

    def call(self, command: str, *, wait_seconds: float | None = None, **arguments) -> dict:
        folder, session = self.discover()
        identifier = uuid.uuid4().hex
        request = folder / f"{identifier}.request.json"
        response = folder / f"{identifier}.response.json"
        staging = folder / f"{identifier}.tmp"
        wait = self.timeout if wait_seconds is None else wait_seconds
        if not 0 < wait <= 55:
            raise ValueError("Request timeout must be greater than zero and at most 55 seconds")
        deadline = time.time() + wait
        monotonic_deadline = time.monotonic() + wait
        payload = {"session": session["session"], "deadline_ms": int(deadline * 1000),
                   "request": {"command": command, **arguments}}
        encoded = json.dumps(payload, allow_nan=False).encode("utf-8")
        if len(encoded) > 1024 * 1024:
            raise ValueError("Request exceeds 1 MiB")
        try:
            with staging.open("xb") as stream:
                stream.write(encoded)
            os.replace(staging, request)
            while time.monotonic() < monotonic_deadline:
                if response.exists():
                    result = json.loads(response.read_text(encoding="utf-8"))
                    if not result.get("ok"):
                        raise RuntimeError(result.get("error", "Editor request failed"))
                    return result["result"]
                time.sleep(0.05)
            raise TimeoutError("Editor did not respond. An edit already executing may finish; inspect before retrying. "
                               "Requests are never automatically retried.")
        finally:
            for path in (staging, request, response):
                path.unlink(missing_ok=True)


class SourceKnowledge:
    extensions = {".cpp", ".h", ".hpp", ".c", ".md", ".txt", ".py", ".ps1", ".glsl", ".inc",
                  ".vert", ".frag", ".geom", ".inl", ".rc", ".def", ".cmake", ".vcxproj", ".sln"}
    excluded = {".git", ".vs", ".codex", ".codex-tmp", "build", "out", "x64", "debug", "release",
                "libraries", "node_modules", "__pycache__", ".venv", "release-builds"}

    def __init__(self, editor_root: Path, reference_root: Path | None):
        self.roots = {"editor": editor_root.resolve()}
        if reference_root:
            self.roots["reference"] = reference_root.resolve()
        for name, root in self.roots.items():
            if not root.is_dir():
                raise ValueError(f"Source root {name} does not exist: {root}")

    def resolve(self, root: str, relative: str) -> Path:
        base = self.roots[root]
        path = (base / relative).resolve()
        parts = path.relative_to(base).parts
        if any(part.lower() in self.excluded for part in parts) or path.suffix.lower() not in self.extensions:
            raise ValueError("Only source/documentation outside generated/private directories is available")
        if path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("Source file exceeds 8 MiB")
        return path

    def read(self, root: str, path: str, line: int = 1, count: int = 100) -> dict:
        if line < 1 or not 1 <= count <= 300:
            raise ValueError("line must be >=1; count must be 1..300")
        resolved = self.resolve(root, path)
        lines = resolved.read_text(encoding="utf-8-sig", errors="replace").splitlines()
        return {"root": root, "path": path, "first_line": line, "total_lines": len(lines),
                "text": "\n".join(f"{i+1}: {lines[i]}" for i in range(line-1, min(len(lines), line-1+count)))}

    def search(self, query: str, root: str = "editor", path_filter: str = "", limit: int = 30) -> dict:
        if not query or len(query) > 256 or not 1 <= limit <= 100:
            raise ValueError("Use a literal query of 1..256 characters and limit 1..100")
        base = self.roots[root]
        hits = []
        for directory, folders, files in os.walk(base, followlinks=False):
            folders[:] = sorted(f for f in folders if f.lower() not in self.excluded
                                and not (Path(directory) / f).is_symlink())
            for filename in sorted(files):
                path = Path(directory) / filename
                relative = path.relative_to(base).as_posix()
                if path_filter.lower() not in relative.lower() or path.suffix.lower() not in self.extensions:
                    continue
                try:
                    safe = self.resolve(root, relative)
                    for number, text in enumerate(safe.read_text(encoding="utf-8-sig", errors="replace").splitlines(), 1):
                        if query.lower() in text.lower():
                            hits.append({"path": relative, "line": number, "text": text[:1000]})
                            if len(hits) == limit:
                                return {"root": root, "hits": hits, "limit_reached": True}
                except (OSError, ValueError):
                    continue
        return {"root": root, "hits": hits, "limit_reached": False}
