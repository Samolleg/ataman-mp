"""Independent official MCP Python SDK client. Install SDK in an isolated target.

Usage: MCP_SDK_PATH=<isolated SDK target> python ai_agent_sdk_check.py <bridge> <run-dir>
No editor launch, credentials, model calls, or real agent configuration access.
"""
import asyncio
import json
import os
from pathlib import Path
import sys
import site
if os.environ.get("MCP_SDK_PATH"):
    site.addsitedir(os.environ["MCP_SDK_PATH"])
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from jsonschema import Draft202012Validator


async def main():
    exe = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    root.mkdir()
    env = {k: str(root / k.lower()) for k in ["APPDATA", "LOCALAPPDATA", "USERPROFILE", "TEMP", "TMP", "CODEX_HOME", "CLAUDE_CONFIG_DIR"]}
    for path in env.values():
        Path(path).mkdir(exist_ok=True)
    env["EI_MAPPER_AUTOMATION_DIR"] = str(root / "discovery")
    env["PATH"] = os.environ["PATH"]
    results = []
    for era in ["modern", "legacy"]:
        async with stdio_client(StdioServerParameters(command=str(exe), env=env, cwd=root)) as streams:
            async with ClientSession(streams[0], streams[1]) as client:
                hello = await (client.discover() if era == "modern" else client.initialize())
                tools = await client.list_tools()
                assert len(tools.tools) == 22
                for tool in tools.tools:
                    schema = tool.model_dump(by_alias=True)["inputSchema"]
                    Draft202012Validator.check_schema(schema)
                resources = await client.list_resources()
                assert len(resources.resources) == 2
                catalog = await client.read_resource("ataman://tools")
                assert len(json.loads(catalog.contents[0].text)["tools"]) == 22
                listed = await client.call_tool("list_instances", {})
                assert not listed.is_error
                assert json.loads(listed.content[0].text)["instances"] == []
                denied = await client.call_tool("get_context", {})
                assert denied.is_error
                ping = await client.send_ping()
                results.append({"era": era, "hello": hello.model_dump(by_alias=True, exclude_none=True), "tools": len(tools.tools), "resources": len(resources.resources), "schemaValidation": "PASS", "disabledReadRefused": True})
    (root / "result.json").write_text(json.dumps(results, indent=2), encoding="utf8")
    print("Official MCP SDK: modern + legacy; tools/resources/schema/ping/OFF PASS")


if __name__ == "__main__":
    asyncio.run(main())
