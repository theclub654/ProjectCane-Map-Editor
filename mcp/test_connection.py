"""Offline tests: no SDK install, running editor, compiler, network or game required."""
import ast
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from editor_connection import EditorConnection, SourceKnowledge


class ConnectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name) / ("a" * 32)
        self.folder.mkdir()
        self.marker = self.folder / "session.json"
        self.marker.write_text(json.dumps({"session": self.folder.name, "protocol": 1}), encoding="utf-8")
        self.connection = EditorConnection(str(self.folder), timeout=0.4)

    def tearDown(self):
        self.temp.cleanup()

    def respond(self, response):
        def worker():
            for _ in range(100):
                requests = list(self.folder.glob("*.request.json"))
                if requests:
                    path = requests[0]
                    payload = json.loads(path.read_text(encoding="utf-8"))
                    self.assertEqual(payload["session"], self.folder.name)
                    self.assertGreater(payload["deadline_ms"], int(time.time() * 1000))
                    target = path.with_name(path.name.replace(".request.", ".response."))
                    staged = target.with_suffix(".tmp")
                    staged.write_text(json.dumps(response), encoding="utf-8")
                    staged.replace(target)
                    return
                time.sleep(0.005)
        thread = threading.Thread(target=worker)
        thread.start()
        self.addCleanup(thread.join)

    def test_success_and_cleanup(self):
        self.respond({"ok": True, "result": {"revision": "123", "data": {"count": 148}}})
        self.assertEqual(self.connection.call("cids")["data"]["count"], 148)
        self.assertEqual(list(self.folder.glob("*.request.json")), [])
        self.assertEqual(list(self.folder.glob("*.response.json")), [])

    def test_editor_errors_propagate(self):
        self.respond({"ok": False, "error": "Document changed"})
        with self.assertRaisesRegex(RuntimeError, "Document changed"):
            self.connection.call("rename", revision="stale")

    def test_timeout_cleans_request(self):
        with self.assertRaises(TimeoutError):
            self.connection.call("state")
        self.assertEqual(list(self.folder.glob("*.request.json")), [])

    def test_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            self.connection.call("transform", matrix=[float("nan")])

    def test_oversized_rejected(self):
        with self.assertRaisesRegex(ValueError, "1 MiB"):
            self.connection.call("rename", name="a" * (1024 * 1024))

    def test_expired_session_rejected(self):
        with patch("editor_connection.time.time", return_value=time.time() + 30):
            with self.assertRaisesRegex(RuntimeError, "0 live"):
                self.connection.discover()

    def test_wrong_session_rejected(self):
        self.marker.write_text('{"session":"different","protocol":1}')
        with self.assertRaises(RuntimeError):
            self.connection.discover()

    def test_multiple_sessions_require_choice(self):
        parent = Path(self.temp.name) / "projectcane-mcp"
        parent.mkdir()
        for name in ("b" * 32, "c" * 32):
            folder = parent / name
            folder.mkdir()
            (folder / "session.json").write_text(json.dumps({"session": name, "protocol": 1}))
        with patch("editor_connection.tempfile.gettempdir", return_value=self.temp.name):
            with self.assertRaisesRegex(RuntimeError, "2 live"):
                EditorConnection().discover()


class KnowledgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        (self.base / "example.cpp").write_text("first\nSourceEvidence\nthird\n", encoding="utf-8")
        (self.base / ".git").mkdir()
        (self.base / ".git" / "private.txt").write_text("SourceEvidence")
        self.knowledge = SourceKnowledge(self.base, None)

    def tearDown(self):
        self.temp.cleanup()

    def test_numbered_source(self):
        self.assertEqual(self.knowledge.read("editor", "example.cpp", 2, 1)["text"], "2: SourceEvidence")

    def test_search_excludes_private(self):
        hits = self.knowledge.search("sourceevidence")["hits"]
        self.assertEqual([(h["path"], h["line"]) for h in hits], [("example.cpp", 2)])

    def test_escape_rejected(self):
        with self.assertRaises(ValueError):
            self.knowledge.read("editor", "../outside.cpp")

    def test_private_path_rejected(self):
        with self.assertRaises(ValueError):
            self.knowledge.read("editor", ".git/private.txt")

    def test_binary_path_rejected(self):
        (self.base / "map.brx").write_bytes(b"SourceEvidence")
        with self.assertRaises(ValueError):
            self.knowledge.read("editor", "map.brx")

    def test_missing_reference_root_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not exist"):
            SourceKnowledge(self.base, self.base / "missing")

    def test_symlink_escape_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            target = Path(outside) / "outside.cpp"
            target.write_text("private")
            link = self.base / "link.cpp"
            try:
                link.symlink_to(target)
            except OSError:
                self.skipTest("Symlink creation unavailable for this user")
            with self.assertRaises(ValueError):
                self.knowledge.read("editor", "link.cpp")

    def test_bounds(self):
        for line, count in ((0, 100), (1, 301)):
            with self.assertRaises(ValueError):
                self.knowledge.read("editor", "example.cpp", line, count)

    def test_source_limit(self):
        self.assertTrue(self.knowledge.search("i", limit=1)["limit_reached"])


class SourceContractTests(unittest.TestCase):
    def test_mcp_derived_fields_follow_inspector_and_loader(self):
        editor = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        operations = (editor / "EditorMcpOperations.cpp").read_text(encoding="utf-8-sig")
        for fragment in ("object.hidden = p.boolValue", "object.maximumRenderDistance = p.floatValue[0]",
                         "object.celBorderMaximumDistance = p.floatValue[0]", "effect.originKind = origin"):
            self.assertIn(fragment, operations)
        self.assertIn("doc.SynchronizeDerivedObjectRecord(index)", operations)

    def test_file_preview_and_writer_share_argument_validation(self):
        editor = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        application = (editor / "EditorApplication.cpp").read_text(encoding="utf-8-sig")
        files = (editor / "EditorMcpFiles.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("EditorMcp::ValidateFileRequest(document, operation, arguments)", application)
        self.assertIn("const auto destination = ValidateFileRequest(doc, operation, a)", files)
        self.assertNotIn("Owner(a, doc)", files) # Native save accepts resolved PROXY members.
        for fragment in ('doc.Globs().at(Integer(a, "resource_index"))',
                         'doc.Globsets().at(Integer(a, "resource_index"))',
                         'doc.Objects().at(Integer(a, "index"))'):
            self.assertIn(fragment, files)

    def test_invalid_view_settings_cannot_partially_move_camera(self):
        editor = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        application = (editor / "EditorApplication.cpp").read_text(encoding="utf-8-sig")
        view = application.split('if (command == "view")', 1)[1].split('if (command == "visibility_query")', 1)[0]
        self.assertLess(view.index('throw std::runtime_error("Unknown viewport option")'),
                        view.index('camera.SetView('))
        self.assertIn('request.contains("position") != request.contains("direction")', view)

    def test_native_mcp_project_dependency_and_utf8_path_api(self):
        editor = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        files = (editor / "EditorMcpFiles.cpp").read_text(encoding="utf-8-sig")
        self.assertIn('#include "EditorProject.h"', files)
        for path in [*editor.glob("EditorMcp*.cpp"), editor / "EditorApplication.cpp"]:
            self.assertNotIn("std::filesystem::u8path(", path.read_text(encoding="utf-8-sig"), path.name)
        tools = (editor / "EditorMcpTools.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("std::u8string value", tools)
        self.assertIn("return std::filesystem::path(value)", tools)

    def test_native_modules_are_registered_for_both_platform_builds(self):
        import xml.etree.ElementTree as ET
        root = Path(__file__).resolve().parents[1]
        editor = root / "ProjectCane Map Editor"
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        project = ET.parse(editor / "ProjectCane Map Editor.vcxproj")
        namespace = {"p": "http://schemas.microsoft.com/developer/msbuild/2003"}
        for extension, tag in (("cpp", "ClCompile"), ("h", "ClInclude")):
            entries = [node.attrib["Include"] for node in project.findall(f".//p:ItemGroup/p:{tag}", namespace)]
            for path in editor.glob(f"EditorMcp*.{extension}"):
                self.assertEqual(entries.count(path.name), 1, path.name)
                self.assertIn(f'${{EDITOR_DIR}}/{path.name}', cmake)

    def test_python_syntax_and_tool_surface(self):
        base = Path(__file__).resolve().parent
        tree = ast.parse((base / "server.py").read_text(encoding="utf-8"))
        for file in base.glob("*.py"):
            ast.parse(file.read_text(encoding="utf-8"), filename=str(file))
        tools = [node.name for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)
                 and any(isinstance(d, ast.Call) and isinstance(d.func, ast.Attribute)
                         and d.func.attr == "tool" for d in node.decorator_list)]
        self.assertEqual(len(tools), 35)
        self.assertIn("cid_schema", tools)
        self.assertIn("validate_map", tools)
        self.assertNotIn("execute", tools)
        editor = base.parent / "ProjectCane Map Editor"
        application = (editor / "EditorApplication.cpp").read_text(encoding="utf-8-sig")
        self.assertIn("mcpBridge.Poll(handleMcp)", application)
        self.assertIn('request.value("revision", std::string()) != revision()', application)
        self.assertIn('pushDocumentHistory(std::move(before), "MCP " + command)', application)
        self.assertIn("applyProperties(before)", application)
        self.assertIn("CreateObjectFromCatalog(document", application)
        self.assertIn("std::make_unique<BrxDocument>(document)", application)
        self.assertIn("document = std::move(*mcpPreview.document)", application)
        self.assertIn("mcpBridge.allowFileWrites", application)

    def test_native_operation_catalog_matches_dispatch(self):
        import re
        base = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        source = (base / "EditorMcpOperations.cpp").read_text(encoding="utf-8")
        names = re.findall(r'add\("([a-z_]+)"', source)
        dispatch = set(re.findall(r'name == "([a-z_]+)"', source))
        self.assertGreaterEqual(len(names), 50)
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(set(names), dispatch)
        operations = (base / "EditorDocumentOperations.h").read_text(encoding="utf-8-sig")
        for method in re.findall(r'EditorDocumentOperations::(\w+)\(', source):
            self.assertRegex(operations, rf'\b{method}\s*\(')

    def test_record_adapter_fields_exist_in_native_structs(self):
        import re
        base = Path(__file__).resolve().parents[1] / "ProjectCane Map Editor"
        header = (base / "BrxDocument.h").read_text(encoding="utf-8-sig")
        header = re.sub(r'//[^\n]*|/\*.*?\*/', '', header, flags=re.S)
        records = (base / "EditorMcpRecords.cpp").read_text(encoding="utf-8")
        count = 0
        for match in re.finditer(r'^MCP_RECORD\((\w+(?:::\w+)?), (.*)\)$', records, flags=re.M):
            typename, fields = match.groups()
            parent, _, nested = typename.partition('::')
            start = re.search(r'\bstruct\s+' + parent + r'\s*\{', header)
            self.assertIsNotNone(start, typename)
            def body_at(position):
                level = 1
                end = position
                while level:
                    level += (header[end] == '{') - (header[end] == '}')
                    end += 1
                return header[position:end-1]
            body = body_at(start.end())
            if nested:
                inner = re.search(r'\bstruct\s+' + nested + r'\s*\{', body)
                self.assertIsNotNone(inner, typename)
                nested_start = header.find(body) + inner.end()
                body = body_at(nested_start)
            for field in re.findall(r'MCP_FIELD\((\w+)\)', fields):
                self.assertRegex(body, rf'\b{field}\b', f'{typename}.{field}')
            count += 1
        self.assertGreaterEqual(count, 50)


if __name__ == "__main__":
    unittest.main(verbosity=2)
