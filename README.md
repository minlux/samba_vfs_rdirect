# Read Direct - A samba VFS module.

This project implements a virtual file system (VFS)module [1] for Samba 4. When using this module, all files will be opend with `O_DIRECT` flag set. Thus a read access to the file will bypass the kernel filesystem cache and read the file content directly from device.

## Build Preparation
To build the module, you have to copy the module source file into the samba build tree.

1. So first of all, you have to checkout the samba sources. You can either clone the entire git repository or just one specific branch/tag you want to use (here i make a shallow clone of tag *samba-4.14.6*).
```
git clone -b samba-4.14.6 https://github.com/samba-team/samba --depth 1
```

2. Copy *vfs_rdirect.c* into the source tree of samba
```
cp vfs_rdirect.c source3/modules
```

3. Include the module into build scripts.

First, add the following paragraph to the end of `source3/modules/wscript_build`
```
bld.SAMBA3_MODULE('vfs_rdirect',
                 subsystem='vfs',
                 source='vfs_rdirect.c',
                 init_function='',
                 internal_module=bld.SAMBA3_IS_STATIC_MODULE('vfs_rdirect'),
                 enabled=bld.SAMBA3_IS_ENABLED_MODULE('vfs_rdirect'))
```

Then - in file `source3/wscript` - add `vfs_rdirect` the list of *default_shared_modules*. Somehow like this:
```
default_shared_modules.extend([...,
                               'vfs_rdirect']) <-- here we are
```


## Build
The module will be build together with the entire samba suite!!! As I just want to use samba as a simple file servier - without all that *Active Directory* and *LDAP* stuff, I was looking for a minimal build configuration. However, the configuration I am using here, is just a result of "Google searches" and "try and error". So the configuration is just something that works for me...

On my Ubuntu 20.04 LTS machine, i had to install some missing dependencies in advance to make the following configuration work:
```
sudo apt-get install \
   flex \
   python3-dev \
   libgnutls28-dev \
   libparse-yapp-perl
```

```
./configure --without-ads \
            --without-ad-dc \
            --without-ldap \
            --without-winbind \
            --without-acl-support \
            --without-cluster-support \
            --without-pam \
            --without-dnsupdate \
            --without-json \
            --without-libarchive \
            --disable-cups \
            --disable-iprint \
            --disable-python \
            --with-shared-modules='!vfs_snapper' \
            --bundled-libraries=ALL
```
Note: As I don't use the option `--enable-fhs` all samba related stuff will be (later on) installed to `/usr/local/samba`. This is different to the way most distributors install samba. They are using an hierarchical file system [2] approach (to install samba "distributed" to /usr, /etc /var, ...).


Then the build process can be started with `make` and finally `make install`.
```
make -j 4
sudo make install
```
Note: You can use the option `-j` to build with multiple threads.


## Usage
### Share
To use the module, define a samba share in `/usr/local/samba/et/smb.conf` that references to *rdirect* as a *vfs object*
```
[xyz]
   comment = Some special device, whose files i want to access directly (bypassing the kernel filesystem cache)
   path = /media/minlux/xyz
   public = yes
   browsable = yes
   guest ok = yes
   read only = yes
   vfs object = rdirect
```

### User
In addition to the definition of a "share", a user is needed. You may add a dedicated "network-user" to your linux system to access the shares (`sudo adduser ...`). Or just use one of the exising users you already have in user system. **In any case**, you have to add this user also to samba.

```
sudo /usr/local/samba/bin/smbpasswd -a <user>
```
Note: The user must have linux access writes (cf. *chmod*, *chown*) to the share(s)!


## Mount
A client can mount such a share like normal. However it (would probably) make sense to set the mount option `cache=none` to disable caching on client side.
On linux a mount can look like this.
```
sudo mount -t cifs -o username=manuel,cache=none,ro //192.168.178.26/xyz ./xyz
```


## Autostart
To let samba automatically start at system start, a *systemd* service script is required to do the job: `/etc/systemd/system/multi-user.target.wants/smbd.service`


todo!!!
```
[Unit]
Description=Samba SMB Daemon
Documentation=man:smbd(8) man:samba(7) man:smb.conf(5)
Wants=network-online.target
After=network.target network-online.target nmb.service winbind.service

[Service]
Type=notify
PIDFile=/usr/local/samba/var/run/smbd.pid
LimitNOFILE=16384
EnvironmentFile=-/usr/local/samba/etc/sysconfig/samba
ExecStart=/usr/local/samba/sbin/smbd --foreground --no-process-group $SMBDOPTIONS
ExecReload=/bin/kill -HUP $MAINPID
LimitCORE=infinity


[Install]
WantedBy=multi-user.target
```


# Read Only Operation
todo



# Appendix

## Samba Configuration
Here are some optional `[global]` configuration options, to be set in `smb.conf`:

Use as file server only
```
server role = standalone server
```

Allow SMBv1 connections
```
server min protocol = NT1
```

Workgroup name
```
workgroup = WORKGROUP
```

Path to "special" files and directories
```
log file = /tmp/samba.log
pid directory = todo
lock dir = todo
state directory = todo
cache directory = todo
ncalrpc dir = todo
```




# References

- [1] https://wiki.samba.org/index.php/Virtual_File_System_Modules
- [2] https://en.wikipedia.org/wiki/Hierarchical_File_System
