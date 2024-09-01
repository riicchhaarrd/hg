# hg
Replaces all instances of function f for the option -f <function> that start with the following prototype:
```c
f([identifier|string], [decimal number|hexadecimal number], ... anything following after will just remain the same
```
Example:
```sh
./hg -b 32 -f REGISTER_EVENT_CALLBACK <FILE> ...
```
```c
REGISTER_EVENT_CALLBACK(WindowCreatedEvent, 0x0, ...)
// Would be replaced with
REGISTER_EVENT_CALLBACK(WindowCreatedEvent, 0x49a1e611, ...)
```
## Usage
```
./hg [-f FUNCTION_NAME]... [-b BITS] [INPUT_FILES]...
```
## Building
```
gcc main.c -o hg
```
## Notes
- The -b option should be either 32 or 64. If not specified, the program will by default output 64-bit hashes.
- For the hashing algorithm fnv1a_32 and fnv1a_64 are used.
  https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function

