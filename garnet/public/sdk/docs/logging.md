# Logging

The preferred way to publish logs is to use the `syslog` API, currently
available for C in `//pkg/syslog`.

The library provides the ability to tag logs so that they can later be filtered
upon retrieval.

In order to get logs from a device, open a shell on the device as described in
[this document](ssh.md) and run:
```
$ log_listener
```

To view specifics logs, add a tag specification:
```
$ log_listener --tag foobar
```