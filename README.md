Overview
========

Pluginbox is a special Kannel box that sits between bearerbox and smsbox and 
allows plugins to execute and manipulate messages in both directions.

Pluginbox behaves similar to other Kannel boxes and share a compatible
configuration file format and command line options.

Installation
============
Please read the INSTALL file for further instructions. If in a hurry, the quick
explanation is:

```
./bootstrap
./configure
make
```

And finally, as root:

```
make install
```

Included plugins
============
For example purposes I have included an HTTP plugin which can intercept all messages to and from Kannel.

Please see contrib/plugin-http/pluginbox_http.conf as well as example/pluginbox.conf.example for an example of how to configure.

There is also a PHP script example showing how to reject messages as well as modify parameters of the message structures in (contrib/plugin-http/http-processor.php)[https://github.com/donald-jackson/kannel-pluginbox/blob/master/contrib/plugin-http/http-processor.php]

You need to have a compiled version of Kannel available in order to compile
pluginbox.

The Userguide has also valuable information about the install and configuration
steps.

Help
====

The best to ask for help is on Kannel's mailing lists.

Please visit Kannel's site for more information:

http://www.kannel.org/
