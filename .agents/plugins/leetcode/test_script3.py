import asyncio

from leetcode_mcp.client import LeetCodeClient


async def main() -> None:
    client = LeetCodeClient()
    res = await client.fetch_problem(200)
    print("CPP_SNIPPET:\n", res.cpp_snippet)
    await client.close()


if __name__ == "__main__":
    asyncio.run(main())
