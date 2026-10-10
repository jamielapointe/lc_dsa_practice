"""Pytest fixtures and mock payloads for LeetCode MCP test suite."""

from typing import Any

import pytest

from leetcode_mcp.models import ProblemDetails


@pytest.fixture
def sample_problem_details_dict() -> dict[str, Any]:
    """Fixture providing a complete dictionary representation of LeetCode #2."""
    return {
        'frontend_id': 2,
        'question_id': 2,
        'title': 'Add Two Numbers',
        'slug': 'add-two-numbers',
        'difficulty': 'Medium',
        'is_paid_only': False,
        'description_markdown': (
            'You are given two non-empty linked lists representing two non-negative integers. '
            'The digits are stored in reverse order, and each of their nodes contains a single '
            'digit. Add the two numbers and return the sum as a linked list.'
        ),
        'cpp_snippet': (
            '/**\n'
            ' * Definition for singly-linked list.\n'
            ' * struct ListNode {\n'
            ' *     int val;\n'
            ' *     ListNode *next;\n'
            ' *     ListNode() : val(0), next(nullptr) {}\n'
            ' *     ListNode(int x) : val(x), next(nullptr) {}\n'
            ' *     ListNode(int x, ListNode *next) : val(x), next(next) {}\n'
            ' * };\n'
            ' */\n'
            'class Solution {\n'
            'public:\n'
            '    ListNode* addTwoNumbers(ListNode* l1, ListNode* l2) {\n'
            '        return nullptr;\n'
            '    }\n'
            '};'
        ),
        'sample_test_case': '[2,4,3]\n[5,6,4]',
        'example_test_cases': [
            '[2,4,3]\n[5,6,4]',
            '[0]\n[0]',
            '[9,9,9,9,9,9,9]\n[9,9,9,9]',
        ],
        'constraints': [
            'The number of nodes in each linked list is in the range [1, 100].',
            '0 <= Node.val <= 9',
            'It is guaranteed that the list represents a number that does not have leading zeros.',
        ],
        'topic_tags': ['Linked List', 'Math', 'Recursion'],
        'hints': [],
    }


@pytest.fixture
def sample_problem_metadata_dict() -> dict[str, Any]:
    """Fixture providing problem metadata dictionary."""
    return {
        'frontend_id': 2,
        'question_id': 2,
        'title': 'Add Two Numbers',
        'slug': 'add-two-numbers',
        'difficulty': 'Medium',
        'is_paid_only': False,
        'topic_tags': ['Linked List', 'Math', 'Recursion'],
        'hints': [],
    }


@pytest.fixture
def sample_problem_details(sample_problem_details_dict: dict[str, Any]) -> ProblemDetails:
    """Fixture providing a validated ProblemDetails model instance."""
    return ProblemDetails.model_validate(sample_problem_details_dict)


@pytest.fixture
def sample_graphql_question_list_payload() -> dict[str, Any]:
    """Fixture simulating LeetCode problemsetQuestionList GraphQL response for query '2'."""
    return {
        'data': {
            'problemsetQuestionList': {
                'total': 3,
                'questions': [
                    {
                        'frontendQuestionId': '650',
                        'title': '2 Keys Keyboard',
                        'titleSlug': '2-keys-keyboard',
                    },
                    {
                        'frontendQuestionId': '2',
                        'title': 'Add Two Numbers',
                        'titleSlug': 'add-two-numbers',
                    },
                    {
                        'frontendQuestionId': '1017',
                        'title': 'Convert to Base -2',
                        'titleSlug': 'convert-to-base-2',
                    },
                ],
            }
        }
    }


@pytest.fixture
def sample_graphql_question_detail_payload() -> dict[str, Any]:
    """Fixture simulating LeetCode getQuestionDetail GraphQL response for 'add-two-numbers'."""
    return {
        'data': {
            'question': {
                'questionId': '2',
                'questionFrontendId': '2',
                'title': 'Add Two Numbers',
                'titleSlug': 'add-two-numbers',
                'difficulty': 'Medium',
                'isPaidOnly': False,
                'content': (
                    '<p>You are given two non-empty linked lists.</p>\n'
                    '<p><strong>Constraints:</strong></p>\n'
                    '<ul>\n'
                    '<li>The number of nodes in each linked list is in the range '
                    '<code>[1, 100]</code>.</li>\n'
                    '<li><code>0 &lt;= Node.val &lt;= 9</code></li>\n'
                    '<li><code>1 &lt;= n &lt;= 10<sup>4</sup></code></li>\n'
                    '</ul>'
                ),
                'codeSnippets': [
                    {'langSlug': 'python3', 'code': 'class Solution: pass'},
                    {
                        'langSlug': 'cpp',
                        'code': (
                            'class Solution {\npublic:\n    ListNode* addTwoNumbers(ListNode* l1, ListNode* l2) {}\n};'
                        ),
                    },
                ],
                'sampleTestCase': '[2,4,3]\n[5,6,4]',
                'exampleTestcaseList': ['[2,4,3]\n[5,6,4]', '[0]\n[0]'],
                'topicTags': [{'name': 'Linked List'}, {'name': 'Math'}, {'name': 'Recursion'}],
                'hints': [],
            }
        }
    }
