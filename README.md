# SO2

## Dining philosophers problem

In the dining philosophers problem a group of N philosophers is trying to eat.
Each of them requires two chopsticks for eating and has one of his chopsticks
on the left and one on the right. However, the philosophers sit side by side
and when they try to it it's possible that one or both or they chopsticks is
taken by their neighbours.

This repository implements a simplified version of K. M. Chandy and J. Misra
algorithm introduced in the paper [The Drinking Philosophers
Problem](https://dl.acm.org/doi/abs/10.1145/1780.1804). In their version of the
solution each philosopher can request a chopstick from neighbouring philosophers.
Their algorithm relies on the fact that requests are analysed in FIFO order.
Which prevents double eating by single philosopher.

To achive this I implemented FIFOMutex class which ensures that when multiple
threads try to acquire a mutex they will be granted access to the resource in
order.
