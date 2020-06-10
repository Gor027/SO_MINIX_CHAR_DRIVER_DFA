# -*- coding: utf-8 -*-
# Copyright 2020 Michał Radwański 
# Permission is hereby granted, free of charge, to any person obtaining a copy 
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
# copies of the Software, and to permit persons to whom the Software is 
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
# SOFTWARE.


from __future__ import print_function
import fcntl
import os
import errno

class Dfa:
    DFAIOCRESET  = 0x20006101
    DFAIOCADD    = 0x80036102
    DFAIOCACCEPT = 0x80016103
    DFAIOCREJECT = 0x80016104
    def __init__(self, device='/dev/dfa', mode='rb+'):
        self._device = device
        self._mode = mode
    def __enter__(self):
        self._fd = open(self._device, self._mode)
        self.read = self._fd.read
        self.write = self._fd.write
        self.flush = self._fd.flush
        return self
    def __exit__(self, type, value, traceback):
        if type is not None:
            print("exiting with exception:", type, value, traceback)
            if isinstance(value, IOError) and value.errno == errno.ENOTTY:
                print("It's your fault. Possible solution by Wiktor Krasnicki on template ambulance, Sunday 8:57 PM.")

        self._fd.close()
        return True
    def reset(self):
        return fcntl.ioctl(self._fd, Dfa.DFAIOCRESET)
    def add(self, p, a, q):
        arg = [chr(x) for x in [p, a, q] if 0 <= p < 256]
        if len(arg) != 3:
            raise ValueError('value not in range')
        arg = ''.join(arg)
        return fcntl.ioctl(self._fd, Dfa.DFAIOCADD, arg)
    def accept(self, p):
        if 0 <= p < 256:
            arg = chr(p)
        else:
            raise ValueError('value not in range')
        return fcntl.ioctl(self._fd, Dfa.DFAIOCACCEPT, arg)
    def reject(self, p):
        if 0 <= p < 256:
            arg = chr(p)
        else:
            raise ValueError('value not in range')
        return fcntl.ioctl(self._fd, Dfa.DFAIOCREJECT, arg)
    def reset_device(self):
        for p in range(256):
            self.reject(p)
            for a in range(256):
                self.add(p, a, 0)
    def fin(self):
        r = self.read(1)
        if r not in 'YN':
            raise ValueError('got invalid byte:', r)
        return r == 'Y'

def dfa_odd_length(dfa):
    dfa.reset_device()
    dfa.reset()
    for a in range(256):
        dfa.add(0, a, 1)
    dfa.accept(1)

def accept_odd_length():
    with Dfa() as dfa:
        dfa_odd_length(dfa)
        total = 0
        for k in range(10**2):
            writelen = ord(os.urandom(1))
            readlen  = ord(os.urandom(1))
            r = dfa.read(readlen)
            if len(r) != readlen:
                print('failed read:', len(r), 'of', readlen)
            if not all(c == 'NY'[total%2] for c in r):
                print('bad output:', r, k, total)
                return
            data = os.urandom(writelen)
            dfa.write(data)
            total += writelen

def update():
    os.system('service update /service/dfa')

def preserve_after_update():
    with Dfa() as dfa:
        dfa_odd_length(dfa)
        assert not dfa.fin(), 'Bad init state'
        dfa.write('x')
        assert dfa.fin(), 'Does not accept odd length before update'

    update()

    with Dfa() as dfa:
        assert dfa.fin(), 'Does not accept odd length after update'
        dfa.write('x')
        assert not dfa.fin(), 'Bad fini state'

def bigtests():
    with Dfa() as dfa:
        dfa_odd_length(dfa)
        dfa.write('x'*(10**7+1))
        r = dfa.read(10**7)
        assert len(r) == 10**7 and all(c == 'Y' for c in r)
        dfa.write('x'*(10**7+1))
        r = dfa.read(10**7)
        assert len(r) == 10**7 and all(c == 'N' for c in r)


if not os.path.exists('/dev/dfa'):
    os.system('mknod /dev/dfa c 20 0')
    os.system('service up /service/dfa -dev /dev/dfa')

if __name__ == '__main__':
    accept_odd_length()
    preserve_after_update()
    bigtests()
