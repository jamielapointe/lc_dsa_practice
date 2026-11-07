//! Singly-linked list node and memory-safe helpers compatible with LeetCode.

/// Singly-linked list node compatible with LeetCode's canonical definition.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ListNode {
    /// Integer value contained in this node.
    pub val: i32,
    /// Pointer to the subsequent node, if present.
    pub next: Option<Box<Self>>,
}

impl ListNode {
    /// Constructs a new node with the given value and `None` as the next pointer.
    ///
    /// # Examples
    ///
    /// ```
    /// use leet_code::list_node::ListNode;
    ///
    /// let node = ListNode::new(42);
    /// assert_eq!(node.val, 42);
    /// assert!(node.next.is_none());
    /// ```
    #[must_use]
    pub const fn new(val: i32) -> Self {
        Self { val, next: None }
    }
}

impl Default for ListNode {
    /// Default node initialized with value 0 and no next node.
    fn default() -> Self {
        Self::new(0)
    }
}

impl Drop for ListNode {
    /// Iteratively deallocates nodes to prevent call stack overflow on deep lists.
    fn drop(&mut self) {
        let mut cur = self.next.take();
        while let Some(mut boxed_node) = cur {
            cur = boxed_node.next.take();
        }
    }
}

/// Constructs a singly-linked list from a slice of integers.
///
/// Returns `None` if the input slice is empty.
///
/// # Examples
///
/// ```
/// use leet_code::list_node::create_linked_list;
///
/// let head = create_linked_list(&[1, 2, 3]);
/// assert!(head.is_some());
/// ```
#[must_use]
pub fn create_linked_list(values: &[i32]) -> Option<Box<ListNode>> {
    let mut head = None;
    for &val in values.iter().rev() {
        let mut node = Box::new(ListNode::new(val));
        node.next = head;
        head = Some(node);
    }
    head
}

/// Converts a linked list reference into a `Vec<i32>` of element values.
///
/// # Examples
///
/// ```
/// use leet_code::list_node::{create_linked_list, linked_list_to_vec};
///
/// let head = create_linked_list(&[1, 2, 3]);
/// let vec = linked_list_to_vec(head.as_deref());
/// assert_eq!(vec, vec![1, 2, 3]);
/// ```
#[must_use]
pub fn linked_list_to_vec(mut head: Option<&ListNode>) -> Vec<i32> {
    let mut result = Vec::new();
    while let Some(node) = head {
        result.push(node.val);
        head = node.next.as_deref();
    }
    result
}
