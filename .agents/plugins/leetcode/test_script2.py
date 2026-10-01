import asyncio

from leetcode_mcp.client import LeetCodeClient
from leetcode_mcp.exceptions import PremiumProblemError


async def main() -> None:
    client = LeetCodeClient()
    try:
        res = await client.fetch_problem(489)
        print("SUCCESS:", res.description_markdown[:100])
    except PremiumProblemError as e:
        print("PREMIUM ERROR:", e)
    finally:
        await client.close()


if __name__ == "__main__":
    asyncio.run(main())
