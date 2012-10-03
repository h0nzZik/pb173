#!/bin/env python
import os
import array
import fcntl

def test_rdwr():
#	ioctl_read = 0x80083f00	# 64b Debian
	ioctl_read = 0x80043f00	# 32b Fedora
	ioctl_write= 0x40043f01
	fd=os.open("/dev/pb173", os.O_RDWR)
	st=array.array('l', [0])


	print '-- reading --'
	if fcntl.ioctl(fd, ioctl_read, st, True) != 0:
		raise IOError
	print 'Default len == ' + str(st[0])
	print 'read() == \"' + os.read(fd, 200) + '\"'

	print 'setting len to 2'
	if fcntl.ioctl(fd, ioctl_write, 2) != 0:
		raise IOError
	print 'read() == \"' + os.read(fd, 200) + '\"'

	print '-- writing --'
	print 'written ' + str(os.write(fd, "cest praci")) + " bytes"
	os.close(fd)

test_rdwr()

