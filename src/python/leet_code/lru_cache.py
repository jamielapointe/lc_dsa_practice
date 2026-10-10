"""LeetCode #146: LRU Cache (Medium).

URL: https://leetcode.com/problems/lru-cache/

Design a data structure that follows the constraints of a Least Recently Used (LRU) cache:

* ``LruCache(capacity)`` initializes the cache with a positive ``capacity``.
* ``get(key)`` returns the value of ``key`` if it exists, otherwise ``-1``.
* ``put(key, value)`` updates the value of ``key`` if it exists; otherwise it inserts the pair. When the number
  of keys exceeds ``capacity``, the least recently used key is evicted.

Both ``get`` and ``put`` must run in O(1) average time.

Constraints:
    * ``1 <= capacity <= 3000``
    * ``0 <= key <= 10**4``
    * ``0 <= value <= 10**5``
    * At most ``2 * 10**5`` calls will be made to ``get`` and ``put``.

Implementation: a hash map from key to node plus a doubly linked list ordered from most to least recently used,
with sentinel head/tail nodes so insertion and removal never branch on empty neighbours.
"""

from __future__ import annotations


class _Node:
    """A doubly linked list node holding one cache entry."""

    __slots__ = ('key', 'next', 'prev', 'value')

    def __init__(self, key: int = 0, value: int = 0) -> None:
        """Initializes an unlinked node.

        Args:
            key: The cache key.
            value: The cached value.
        """
        self.key = key
        self.value = value
        self.prev: _Node = self
        self.next: _Node = self


class LruCache:
    """Fixed-capacity least-recently-used cache with O(1) ``get`` and ``put``."""

    def __init__(self, capacity: int) -> None:
        """Initializes the cache.

        Args:
            capacity: Maximum number of entries; must be positive.

        Raises:
            ValueError: If ``capacity`` is not positive.
        """
        if capacity <= 0:
            message = 'capacity must be positive'
            raise ValueError(message)
        self._capacity = capacity
        self._nodes: dict[int, _Node] = {}
        # Sentinels: head.next is the most recently used node, tail.prev the least recently used.
        self._head = _Node()
        self._tail = _Node()
        self._head.next = self._tail
        self._tail.prev = self._head

    def __len__(self) -> int:
        """Returns the number of cached entries."""
        return len(self._nodes)

    def __contains__(self, key: object) -> bool:
        """Returns whether ``key`` is cached, without affecting recency."""
        return key in self._nodes

    @property
    def capacity(self) -> int:
        """The maximum number of entries."""
        return self._capacity

    def get(self, key: int) -> int:
        """Returns the cached value and marks ``key`` as most recently used.

        Args:
            key: The key to look up.

        Returns:
            The cached value, or ``-1`` if ``key`` is not cached.
        """
        node = self._nodes.get(key)
        if node is None:
            return -1
        self._unlink(node)
        self._push_front(node)
        return node.value

    def put(self, key: int, value: int) -> None:
        """Inserts or updates ``key`` and marks it most recently used, evicting the LRU entry if needed.

        Args:
            key: The key to store.
            value: The value to associate with ``key``.
        """
        node = self._nodes.get(key)
        if node is not None:
            node.value = value
            self._unlink(node)
            self._push_front(node)
            return

        if len(self._nodes) == self._capacity:
            least_recent = self._tail.prev
            self._unlink(least_recent)
            del self._nodes[least_recent.key]

        node = _Node(key, value)
        self._nodes[key] = node
        self._push_front(node)

    @staticmethod
    def _unlink(node: _Node) -> None:
        """Detaches ``node`` from the list."""
        node.prev.next = node.next
        node.next.prev = node.prev

    def _push_front(self, node: _Node) -> None:
        """Inserts ``node`` right after the head sentinel (most recently used position)."""
        node.prev = self._head
        node.next = self._head.next
        self._head.next.prev = node
        self._head.next = node


# LeetCode compatibility alias: the official Python 3 template names the class ``LRUCache``.
LRUCache = LruCache
