# COL 334/672 | Piazza QA

**Post @16: COL334: Assignment 2**
*Author:* Shailesh Dagar
*Updated:* 4 days ago

Hello folks,
The problem statement for assignment 2 and all the required details are available on moodlenew. The handout is quite verbose as we used a lot of examples to make instructions as clear as possible.

**A few important details:**
* The assignment is to be completed in teams of at most 2 members.
* Submission will be through Gradescope.
* Deadline: 10 September 2026, 11:59 PM IST.
* Only one teammate needs to submit on Gradescope. Please make sure to add your teammate to the submission.
* Please follow the submission format and filename requirements specified in the handout.
* Late submissions: A penalty of 20% per day will be applied, for a maximum of 3 days. After 3 days, the submission portal will close and no further submissions will be accepted.

For any questions or clarifications, please post them in this thread so that everyone can benefit from the discussion.

*Note:* Since, the assignment documentation is quite long it is possible that there might be errors. Or if you feel something is not properly defined, feel free to ask in this thread with proper references to the concerned section(s)/subsection(s) from the handout. We'll patch the handout if needed.

**Updates:**
* **[01/09/26 19:27]:** Minor update to Assignment handout to properly define the semantics for the 'OK' message sent by the Exchange Server. Also, fixed subsection numbering in Section 0 and Section 1.
* **[04/09/26 02:13]:** C++ is now an allowed language for implementation. Handout has been updated to reflect the same.
* **[05/09/26 01:37]:** Numeric ranges have been defined for Order IDs, Quantity, and Price in Section 2.1 of the Assignment Handout.
* **[05/09/26 02:06]:** Defined semantics for Outstanding orders in case of Connection termination at the end of Section 2.6 of the Assignment Handout.

---

## Followup Discussions

### @16_f1: Distinguishing Client Roles upon Connection
**Anonymous Atom:** Section 4.5 states: "The implementation must enforce the distinction between the two client types." However, when a TCP connection is established via `accept()`, the server doesn't know who is connecting. A Trader Client identifies itself via `LOGIN <username>`. But according to the table in Section 2.8, Market-Data Clients do not support the LOGIN command. How exactly does the server know a new connection is a Market-Data Client? Is it simply assumed to be a Market-Data Client the moment it sends a SUBSCRIBE Command without logging in?

**Shailesh Dagar:** Yes, only Traders need to send `LOGIN` messages. MD Clients send `SUBSCRIBE` messages.

---

### @16_f2: Individual vs Pair Submission
**Devansh:** Sir, can we do this assignment individually?

**Anonymous Calc:** There it is written, "Atmost 2 members".

**Devansh:** But I think it is not confirmed from prof's end, that's why I had a doubt when I asked him yesterday.

**Shailesh Dagar:** Try to do it in pairs. Refer to this to find a partner.

---

### @16_f3: C++ Usage
**Anonymous Poet:** Can we use C++?

**Shailesh Dagar:** You may use C++ for implementation. But, the C++ implementations must use the POSIX socket API directly and may not use higher-level C++ networking libraries such as Boost.Asio.

---

### @16_f4: Message Framing & Bounds
**Anonymous Beaker:** The protocol defines message framing purely by delimiter but I couldn't find a stated upper bound on the length of a single message.
1. Is there a maximum length we may assume for one application-level message?
2. If a client sends a line longer than that limit -- or connects and streams bytes without ever sending a newline -- what behaviour is expected?
3. Section 2.1 says quantity and price "must be positive integers" but gives no range. Is there a maximum value we must support?

**Shailesh Dagar:** 
1. The assignment does not impose a specific maximum length for an application-level message. You do not need to define your own protocol-level maximum. Arbitrarily long application messages are outside the required behavior of the assignment.
2. No particular behavior is required for arbitrarily long messages or for a client that sends bytes indefinitely without a terminating `
`. Such inputs are outside the required behavior of the assignment.
3. Yes, we should specify a range. Quantity and price must be values ranging from 1 through 2,147,483,647 inclusive. A value outside this range should be rejected with: `ERROR <reason>`. Use the following range for Order IDs: 0 through 2,147,483,647 inclusive.

---

### @16_f5: Hypervisor Alternatives
**Subarno Saha:** Is QEMU/KVM acceptable in place of VirtualBox? On my Ubuntu host, VirtualBox requires out-of-tree kernel modules that will not load while Secure Boot is enabled. QEMU/KVM uses in-tree kernel modules and has no such issue. Can you confirm QEMU/KVM is acceptable?

**Shailesh Dagar:** Yes, QEMU/KVM is acceptable.

---

### @16_f6: Multiple Commands on Same Socket
**Anonymous Beaker:** What is the expected handling for when we receive 2 `LOGIN` messages for different usernames on the same socket? Do we send back an error and retain the first username / switch to the second username / design choice?

**Shailesh Dagar:** The assignment specifies the behavior of compliant clients, and you should implement that behavior rather than trying to handle every possible malformed or nonsensical message sequence. Messages sent in violation of these role-specific rules are outside the required behavior of the assignment.

**Anonymous Beaker:** Just to confirm, it is valid for the Market-Data client to send 2 different subscribe messages right?

**Shailesh Dagar:** Yes, they can subscribe to both JNST and IMCT.

---

### @16_f7: Outstanding Orders on Disconnect
**Anonymous Scale:** Sir, what if user places an order and then closes the TCP connection. will the order still be on order book. if it is so, then If that order got matched, What to do with the response message?

**Shailesh Dagar:** An outstanding order remains in the order book after the Trader Client's TCP connection is closed. If such an order is subsequently matched, the trade is executed normally. Since the originating client is no longer connected, the Exchange Server does not send the corresponding BOUGHT or SOLD notification for that order. The trade is still reported to subscribed Market-Data Clients through the normal TRADE message.

---

### @16_f8: Trade Notifications
**Anonymous Scale:** Sir, Lets say a Sell order of 40 is matched with 2 buy order of 20. then Should I send `TRADE IMCT 200 40` once or `TRADE IMCT 200 20` twice? or it is a design choice?

**Shailesh Dagar:** These are two different trades. So, we should expect something like two "`TRADE IMCT 200 20`" messages to a subscriber client and two pairs of BOUGHT and SOLD messages to the concerned trader clients.

---

### @16_f9: Same-Client Order Matching
**Anonymous Poet:** Expected behaviour for sell trades?

**Shailesh Dagar:** Orders from the same Trader Client may match each other. It is a valid trade.

---

### @16_f10: Order FIFO Handling
**Anonymous Beaker:** Can we assume FIFO handling of outstanding orders? eg. SELL 2 @ 20 [order id 1], SELL 2 @ 20 [order id 2], BUY 2 @ 20 [order id 3]. Do we have to match 3 to 1 or is it possible that we have 2 partial matches?

**Shailesh Dagar:** There is no concept of partial matches. Order 3 could match with either Order 2 or Order 1. It doesn't matter to which order it matches. We leave that implementation detail up to you. But, it can't be the case here that Order 3 doesn't match with either when there are perfectly eligible candidate orders in the order book.

---

### @16_f11: Handout Changes
**Anonymous Poet:** What was changed in the handout?

**Shailesh Dagar:** Every update to the handout has been listed in the note above, and any further updates will also be listed in a similar manner.

---

### @16_f12: LOGIN Purpose
**Anonymous Mouse:** The handout says the trader client is initiated with username instead. Then, what does LOGIN method is for? Do we have to clear the last session buffer and change the username of the session?

**Shailesh Dagar:** The Trader Client is not assigned a username as part of `connect()`. Establishing the TCP connection merely creates a TCP connection between the client and Exchange Server. After the connection is established, the Trader Client sends: `LOGIN <username>`. The Exchange Server then registers that connection as a Trader Client with that username.

---

### @16_f13: Launcher Scripts
**Anonymous Gear:** Won't the clients be created by the `experiment.py` file itself? why do we need these files and what should they contain?

**Shailesh Dagar:** `client/run-trader` and `client/run-market-data` are launcher scripts for the two types client processes. Read section 6.0.2 "Submission Interface" for details. Even if `experiment.py` creates its own client processes, we still need launcher scripts to test your implementation of exchange server, trader client and market data client.

---

### @16_f14: Reconnecting Users
**Anonymous Helix:** Should reconnecting with the same username allow the user to cancel old orders from that same username, or not? Will they get notifications about orders placed earlier?

**Shailesh Dagar:** A username identifies a Trader Client only for the lifetime of its current TCP connection. If a Trader Client disconnects, another Trader Client may subsequently use the same username. The orders associated with that username will not get cancelled, and may match with their counterpart orders in the future. A newly connected Trader Client does not inherit any orders, notifications, or other state belonging to a previous connection that used the same username. 

**Anonymous Poet 2:** Sir, I believe this design defeats the purpose of a login system... can you clarify the reason as to why multiple connected users can't have the same username?

**Shailesh Dagar:** You're right, but it doesn't matter. The purpose of defining LOGIN, orders, trades, subscriptions, etc. is primarily to provide a simple application on top of TCP so that we can study TCP connections, message framing, concurrent clients, blocking/non-blocking I/O... This is a Computer Networks assignment, not a system-design assignment.

---

### @16_f15: Gradescope Execution Bug
**Anonymous Comp:** "Launcher scripts are executable" fails even though the zip stores 0755. The results page notes the zip is auto-extracted by Gradescope before grading. Python's `zipfile.extract()` ignores stored modes and writes files as 0644. Could you confirm how submissions are extracted, and either fix the check or override the 2 points?

**Dinu Goyal:** Don't worry about this issue... We will fix this on the autograder... The submission is fine if it passes the other checks. It wont affect your final marks at all.

---

### @16_f16: C++ Threading
**Anonymous Atom 2:** Is threads programming for C++ allowed?

**Shailesh Dagar:** Yes, you may use either C or C++ threading facilities (such as POSIX threads or C++ `std::thread`).

---

### @16_f17: Python Kqueue/Select
**Anonymous Calc 2:** are we allowed to use kqueue, kevents related stuff from select library in python ?

**Shailesh Dagar:** Yes.