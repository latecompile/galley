# Fenced Code

A fence swallows blank lines. This is the case a scanner that splits on blank
lines gets wrong, and code is half of a technical book.

```sh
make clean

make
```

That listing is one block, not three.

A tilde fence carries backticks without escaping them:

~~~markdown
```sh
echo "a fence inside a fence"
```
~~~

A longer fence closes only on one at least as long, so a shorter run of
backticks inside it is content:

````text
```
still inside
```
````

An info string on a backtick fence may not itself contain a backtick, so the
line below opens nothing and is an ordinary paragraph: ``` js `x` ```

An unclosed fence runs to the end of the file, and everything after it is
code whatever it looks like:

```c
int main(void) { return 0; }

# not a heading

- not a list
