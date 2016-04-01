## uca-net

A transparent TCP-based network bridge camera for remote access of libuca
cameras.

### Installation and usage

The only dependency is libuca itself and any camera you wish to access. Build
the server and client with

    $ mkdir build && cd build
    $ cmake ..
    $ make && (sudo) make install

Now, you can start a server on a remote machine with

    $ ucad mock

and connect to it from any other machine, e.g.

    $ uca-grab -p host=foo.bar.com:4567 -n 10 net            # grab ten frames
    $ uca-camera-control -c net     # control graphically

or from [Concert](https://github.com/ufo-kit/concert)

```python
from concert.devices.cameras.uca import Camera

camera = Camera('net', {'host': 'foo.bar:1234'})

with camera.recording():
    print(camera.grab())
```
