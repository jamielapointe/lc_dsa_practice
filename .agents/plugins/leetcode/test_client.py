import asyncio

from leetcode_mcp.client import LeetCodeClient


async def main() -> None:
    async with LeetCodeClient() as client:
        p = await client.fetch_problem("200")
        print("---")
        print("snippet:")
        print(p.cpp_snippet)
        print("---")
        print("constraints:")
        print(p.constraints)
        print("---")
        print("description:")
        print(p.description_markdown)


asyncio.run(main())
