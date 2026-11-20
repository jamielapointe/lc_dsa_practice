"""LeetCode #20: Valid Parentheses (Easy).

URL: https://leetcode.com/problems/valid-parentheses/

Given a string ``s`` containing just the characters ``'('``, ``')'``, ``'{'``, ``'}'``,
``'['`` and ``']'``, determine if the input string is valid.

An input string is valid if:

1. Open brackets must be closed by the same type of brackets.
2. Open brackets must be closed in the correct order.
3. Every close bracket has a corresponding open bracket of the same type.

Examples:
    1. ``s = "()"`` -> ``True``
    2. ``s = "()[]{}"`` -> ``True``
    3. ``s = "(]"`` -> ``False``
    4. ``s = "([])"`` -> ``True``
    5. ``s = "([)]"`` -> ``False``

Constraints:
    * ``1 <= len(s) <= 10**4``
    * ``s`` consists of parentheses only ``'()[]{}'``.

Complexity: O(n) time, O(n) space.
"""


def valid_parentheses(s: str) -> bool:
    """Determines whether a bracket string is properly nested and balanced.

    An empty string ``""`` is vacuously valid and returns ``True``. Any character
    outside ``"()[]{}"`` is rejected with a ``ValueError``.

    Args:
        s: Input string expected to consist solely of characters in ``"()[]{}"``.

    Returns:
        ``True`` if every opening bracket in ``s`` is closed by the same bracket type
        in the correct LIFO order (including ``""``), ``False`` otherwise.

    Raises:
        ValueError: If ``s`` contains any character outside ``"()[]{}"``.

    Examples:
        >>> valid_parentheses("()")
        True
        >>> valid_parentheses("()[]{}")
        True
        >>> valid_parentheses("(]")
        False
        >>> valid_parentheses("([])")
        True
        >>> valid_parentheses("([)]")
        False
        >>> valid_parentheses("")
        True
    """
    bracket_stack: list[str] = []
    open_brackets = "([{"
    closed_brackets = ")]}"
    bracket_matches = {
        closed_brackets[0]: open_brackets[0],
        closed_brackets[1]: open_brackets[1],
        closed_brackets[2]: open_brackets[2],
    }
    for c in s:
        if c in open_brackets:
            bracket_stack.append(c)
        elif c in closed_brackets:
            if not bracket_stack or bracket_stack.pop() != bracket_matches[c]:
                return False
        else:
            raise ValueError("Invalid character in string")

    return len(bracket_stack) == 0


class Solution:
    """LeetCode compatibility wrapper matching the official Python 3 signature."""

    def isValid(  # ruff: ignore[invalid-function-name, no-self-use]
        self,
        s: str,
    ) -> bool:
        """Determines whether ``s`` has valid matching brackets.

        Args:
            s: Input string expected to consist solely of characters in ``"()[]{}"``.

        Returns:
            ``True`` if ``s`` is validly parenthesized (including ``""``), ``False``
            otherwise. Propagates ``ValueError`` raised by ``valid_parentheses`` when
            ``s`` contains any character outside ``"()[]{}"``.

        Examples:
            >>> Solution().isValid("()")
            True
            >>> Solution().isValid("()[]{}")
            True
            >>> Solution().isValid("(]")
            False
            >>> Solution().isValid("([])")
            True
            >>> Solution().isValid("([)]")
            False
            >>> Solution().isValid("")
            True
        """
        return valid_parentheses(s)
