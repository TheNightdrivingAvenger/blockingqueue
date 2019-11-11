# Blocking queue

A simple blocking queue that looks similar to BlockingCollection from .NET. Allows thread-safe operations, as well as marking the queue finished and cancelling waiting operations. After marking the queue as finished it will allow take the elements until it's empty, but won't allow to add any. When it is finished and empty, it will return special value from "take element" function.
