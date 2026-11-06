"""Real MCP stdio handshake; no editor mutation, build or network access."""
import asyncio
import base64
from pathlib import Path
import sys
import unittest

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from server import make_server
from editor_connection import SourceKnowledge


class SdkTests(unittest.IsolatedAsyncioTestCase):
    async def test_viewport_requires_both_camera_arguments_before_dispatch(self):
        class FakeConnection:
            def __init__(self):
                self.calls = []
            def call(self, command, **arguments):
                self.calls.append((command, arguments))
                return {"data": arguments}
        connection = FakeConnection()
        root = Path(__file__).resolve().parents[1]
        server = make_server(connection, SourceKnowledge(root, None))
        for arguments in ({"position": [1, 2, 3]}, {"direction": [0, 1, 0]}):
            with self.assertRaisesRegex(Exception, "together"):
                await server.call_tool("viewport", arguments)
        self.assertEqual(connection.calls, [])
        await server.call_tool("viewport", {"position": [1, 2, 3], "direction": [0, 1, 0],
                                            "settings": {"effects": True}})
        self.assertEqual(connection.calls, [("view", {"position": [1.0, 2.0, 3.0],
            "direction": [0.0, 1.0, 0.0], "settings": {"effects": True}})])

    async def test_capture_returns_native_image_content_and_metadata(self):
        class FakeConnection:
            calls = []
            def call(self, command, **arguments):
                self.calls.append(command)
                if len(self.calls) == 1:
                    return {"session": "test", "revision": "opaque", "data": {"pending": True, "capture_ticket": "ticket"}}
                if arguments != {"capture_ticket": "ticket"}:
                    raise AssertionError("Read-only capture ticket polling expected")
                return {"session": "test", "revision": "later", "data": {
                    "png_base64": base64.b64encode(b"fake-png-data").decode(),
                    "capture_token": "capture", "captured_revision": "captured-opaque", "image_width": 1, "image_height": 1}}
        connection = FakeConnection()
        root = Path(__file__).resolve().parents[1]
        server = make_server(connection, SourceKnowledge(root, None))
        result = await server.call_tool("capture_editor_ui", {})
        self.assertEqual([item.type for item in result], ["text", "image"])
        self.assertIn("capture_token", result[0].text)
        self.assertIn("captured-opaque", result[0].text)
        self.assertNotIn("png_base64", result[0].text)
        self.assertEqual(result[1].mimeType, "image/png")
        self.assertEqual(base64.b64decode(result[1].data), b"fake-png-data")
        with self.assertRaisesRegex(Exception, "reserved"):
            await server.call_tool("editor_ui_input", {"capture_token": "capture", "action": "click",
                                                       "arguments": {"command": "create"}})
        self.assertEqual(connection.calls, ["ui_capture", "ui_capture"])

    async def test_stdio_tool_registration_and_source_read(self):
        server = Path(__file__).resolve().with_name("server.py")
        parameters = StdioServerParameters(command=sys.executable, args=["-B", str(server)])
        async with stdio_client(parameters) as (read, write):
            async with ClientSession(read, write) as session:
                await asyncio.wait_for(session.initialize(), 15)
                catalog = await session.list_tools()
                names = {tool.name for tool in catalog.tools}
                self.assertEqual(len(names), 35)
                self.assertTrue({"preview_operation", "apply_preview", "inspect_record", "write_previewed_file"} <= names)
                for tool in catalog.tools:
                    self.assertEqual(tool.inputSchema["type"], "object")
                result = await session.call_tool("read_source", {"path": "MCP.md", "line": 1, "count": 2})
                self.assertFalse(result.isError)
                self.assertIn("ProjectCane", str(result.content))


if __name__ == "__main__":
    unittest.main(verbosity=2)
