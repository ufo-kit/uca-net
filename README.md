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

    $ uca-grab -n 10 net            # grab ten frames
    $ uca-camera-control -c net     # control graphically
