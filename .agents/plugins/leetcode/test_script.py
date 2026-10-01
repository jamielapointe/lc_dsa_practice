import asyncio

from leetcode_mcp.client import LeetCodeClient


async def main() -> None:
    client = LeetCodeClient()
    res = await client.fetch_problem(200)
    print("DESC_MARKDOWN:\n", res.description_markdown)
    await client.close()


if __name__ == "__main__":
    asyncio.run(main())
