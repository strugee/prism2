# Prism 2

Prism 2 is a fork of [Mozilla Prism][1]. It's ultimate goal is currently the version of
Gecko/XULRunner that is supported right now, which is Gecko 20 as of 4/14/13.

## Roadmap

### v1

 - Gecko 2
 
### v1.*

 - Gecko releases > 2 but < current
 
### v2

 - Current Gecko

## Building

### Read this first

Currently Prism uses XULRunner 1.9.0.7. If you want to build with that version, consult
the [Mozilla build documentation][2]. If you're looking to contribute to Prism 2, consult
this document.

Note that because the port to Gecko 2 is not complete, this build will fail.

### Building

 1. Download the [XULRunner 2.0 source][3].

[1]: https://mozillalabs.com/en-US/prism/
[2]: https://developer.mozilla.org/en-US/docs/Prism/Build
[3]: ftp://ftp.mozilla.org/pub/xulrunner/releases/2.0/source/