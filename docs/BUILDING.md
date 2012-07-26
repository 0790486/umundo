# Building from Source

The source code is built using CMake, the process of building umundo is 
essentially the same on every platform:

1. Read the [Platform Notes](#platform-notes) below to prepare your system.
2. Checkout umundo into a convenient directory:

	<tt>git clone git://github.com/tklab-tud/umundo.git</tt>

3. Create a new directory for an *out-of-source* build. I usually create sub-directories 
in <tt><UMUNDO_SRC>/build/</tt>.
4. Run cmake (or ccmake / CMake-GUI) to create the files required by your actual build-system.
5. Use your actual build-system or development environment to build umundo.

If you want to build for another IDE or build-system, just create a new 
*out-of-source* build directory and start over with cmake. To get an idea of 
supported IDEs and build-environments on your platform, type <tt>cmake --help</tt> 
or run the CMake-GUI and look for the *Generators* section at the end of the 
output. Default on Unices is Makefiles.

# Build Dependencies

Overview of the umundo dependencies. See the [Platform Notes](#platform-notes) for details.

<table>
    <tr><th>Platform</th><th>Dependency</th><th>Version</th><th>Comment</th></tr>
	<tr>
		<td rowspan="9"><b>Everyone</b></td>
			<td bgcolor="#ffd"><a href="http://www.cmake.org/cmake/resources/software.html">CMake</a><br />required</td>
			<td>>=&nbsp;2.8.6</td>
			<td>The build-system used for umundo.</td></tr>
		<tr>
			<td bgcolor="#ffd"><a href="http://git-scm.com/">Git</a><br />required</td>
			<td></td>
			<td>Versioning control system.</td></tr>
		<tr>
			<td bgcolor="#ffd"><a href="http://code.google.com/p/protobuf/">Protocol&nbsp;Buffers</a><br />required s11n</td>
			<td>2.4.1 works</td>
			<td>Object serializer currently used. You will have to build them from source in MS Windows (see platform notes).</td></tr>
		<tr>
			<td bgcolor="#ffd"><a href="http://www.pcre.org/">PCRE</a><br />required core</td>
			<td>7.0 works</td>
			<td>Regular expressions implementation for service queries. At the moment in core as Regex.cpp</tr>
		<tr>
			<td bgcolor="#ffd"><a href="http://code.google.com/p/protobuf/">ZeroMQ</a><br />required core</td>
			<td>3.2</td>
			<td>Network socket abstractions, just use the prebuilt binaries that come with the umundo distribution.</td></tr>
		<tr>
			<td bgcolor="#dfd"><a href="http://www.swig.org/">SWIG</a><br />optional</td>
			<td>>=&nbsp;2.0.5</td>
			<td>Wraps the C/C++ code from umundo.core for Java, CSharp and potentially  other languages. Make sure to 
				get version 2.0.5, older ones won't do.</tr>
		<tr>
			<td bgcolor="#dfd"><a href="http://www.oracle.com/technetwork/java/javase/downloads/index.html">Java Developer Kit</a><br />optional</td>
			<td>>=&nbsp;5</td>
			<td>The JDK is required for the umundo.core JNI wrappers and the Java bindings. Only useful if you plan to install SWIG as well.</td></tr>
		<tr>
			<td bgcolor="#dfd"><a href="http://ant.apache.org/bindownload.cgi">Ant</a><br />optional</td>
			<td>>=&nbsp;1.8.x</td>
			<td>Build system used for the Java bindings.</td></tr>
		<tr>
			<td bgcolor="#dff"><a href="http://www.stack.nl/~dimitri/doxygen/">Doxygen</a><br />recommended</td>
			<td></td>
			<td>Used by <tt>make docs</tt> to generate documentation from source comments.</td></tr>
	</tr>
	<tr bgcolor="grey"><td bgcolor="#ddd" colspan="4"></td></tr>
		
	<tr>
		<td rowspan="2"><b>Mac OSX</b></td>
			<td bgcolor="#ffd"><a href="http://developer.apple.com/xcode/">XCode</a><br />required</td>
			<td>4.2.1 works</td>
			<td>Apples SDK with all the toolchains.</td></tr>
		<tr>
			<td bgcolor="#dff"><a href="http://www.macports.org/">MacPorts</a><br />recommended</td>
			<td>>= 2.0.3</td>
			<td>Build system for a wide selection of open-source packages.</td></tr>
	</tr>

	<td rowspan="1"><b>Linux</b></td>
		<td bgcolor="#ffd">Avahi<br />required</td>
		<td>3.x works</td>
		<td>For Debian:<br /><tt>$ sudo apt-get install avahi-daemon libavahi-client-dev</tt></td></tr>
	</tr>

	<tr>
	<td rowspan="3"><b>Windows</b></td>
		<td bgcolor="#ffd"><a href="http://www.microsoft.com/visualstudio/en-us">Visual&nbsp;Studio&nbsp;10</a><br />required</td>
		<td>v10 pro works</td>
		<td>As a student, you can get your version through MSAA.</td></tr>
	<tr>
		<td bgcolor="#dfd">
			<a href="http://www.microsoft.com/net/download">.NET Framework</a><br />optional</td>
		<td>3.5 or 4.0 are fine</td>
		<td>If SWIG is installed and a C# compiler found, the build-process will generate C# bindings.</td></tr>
	</tr>
	<tr>
		<td bgcolor="#dfd">
			<a href="http://support.apple.com/kb/DL999">Bonjour Print Wizard</a> or 
			<a href="http://www.apple.com/itunes/download/">iTunes</a><br />optional</td>
		<td></td>
		<td>If you plan to use the system wide Bonjour service, you will need a mDNSResponder daemon contained in both these 
			distributions. The uMundo libraries from the installers contain an embedded mDNS implementation.</td></tr>
	</tr>
	<tr>

</table>

# Platform Notes

This section will detail the preparation of the respective platforms to ultimately compile umundo.

## Mac OSX

* [<b>Build Reports</b>](http://umundo.tk.informatik.tu-darmstadt.de/cdash/index.php?project=umundo)
* [<b>Precompiled dependencies</b>](https://github.com/tklab-tud/umundo/tree/master/contrib/prebuilt/darwin-i386/gnu/lib)

All the build-dependencies are straight forward to install using <tt>port</tt> 
from [MacPorts](http://www.macports.org/). The rest is most likely distributed 
as precompiled binaries with umundo. I promise to describe the process more 
thoroughly the next time I install on a fresh MacOSX system. For now, just 
install packages via port until all dependencies are met and CMake stops 
complaining while preparing the build-artifacts.

Once you have installed all the dependencies you need, you can invoke CMake to 
create Makefiles or a Xcode project. 

### Console / Make

	$ mkdir -p build/umundo/cli && cd build/umundo/cli
	$ cmake <UMUNDO_SRCDIR>
	[...]
	#
	# TODO: Show how to resolve build-dependencies - For now rely on MacPorts and the respective installers.
	#
	-- Build files have been written to: .../build/umundo/cli
	$ make

You can test whether everything works by starting one of the sample programs:

	$ ./bin/umundo-pingpong &
	$ ./bin/umundo-pingpong &
	oioioioioi
	[...]
	$ killall umundo-pingpong
	
### Xcode
	
	$ mkdir -p build/umundo/xcode && cd build/umundo/xcode
	$ cmake -G Xcode <UMUNDO_SRCDIR>
	[...]
	-- Build files have been written to: .../build/umundo/xcode
	$ open umundo.xcodeproj

You can of course reuse the same source directory for many build directories.

## Linux

* [<b>Build Reports</b>](http://umundo.tk.informatik.tu-darmstadt.de/cdash/index.php?project=umundo)
* [<b>Precompiled dependencies (32Bit)</b>](https://github.com/tklab-tud/umundo/tree/master/contrib/prebuilt/linux-i686/gnu/lib)
/ [<b>Precompiled dependencies (64Bit)</b>](https://github.com/tklab-tud/umundo/tree/master/contrib/prebuilt/linux-x86_64/gnu/lib)

You will have to install all required build-dependencies via your package-manager. 
If your system does not support the minimum version (especially with CMake, SWIG 
and ProtoBuf), you will have to compile the dependency from source.

### Preparing *apt-get based* distributions

For the following instructions, I downloaded and installed a fresh 
[Debian Testing](http://cdimage.debian.org/cdimage/wheezy_di_alpha1/i386/iso-cd/debian-wheezy-DI-a1-i386-netinst.iso)
wheezy Alpha1 release. You can go for Debian Stable as well, but at least 
the ProtoBuf and CMake versions in stable are too old and would need to be pulled 
from testing or compiled from source. The whole process should be pretty similar 
with other *apt-get* based distributions.

	# build system and compiler
	$ sudo apt-get install git cmake cmake-curses-gui make g++

	# umundo required dependencies
	$ sudo apt-get install avahi-daemon libavahi-client-dev libprotoc-dev protobuf-compiler libpcre3-dev

There may still be packages missing due to the set of dependencies among packages 
in other distributons but these are all the packages needed on Debian Testing to 
compile and run the core functionality of umundo. If you want to build the Java 
language bindings, you will need the following packages in addition:

	# umundo optional dependencies - SWIG and the Java Developer Kit
	$ sudo apt-get install swig openjdk-7-jdk ant
	
If CMake still does not find your JDK location, make sure JAVA_HOME is set:

	$ echo $JAVA_HOME
	/usr/lib/jvm/java-7-openjdk-i386

<b>64 bit Note:</b> We do not include 64 bit binaries for ZeroMQ, you will have to compile 
and install [ZeroMQ 3.2](http://download.zeromq.org/zeromq-3.2.0-rc1.tar.gz) by yourself.

### Preparing *yum based* distributions

The following instructions work as they are for 
[Fedora 17 Desktop](http://download.fedoraproject.org/pub/fedora/linux/releases/17/Live/i686/Fedora-17-i686-Live-Desktop.iso).
As with the *apt-get* based distributions, there might be additional packages 
needed for other distributions.

	# build system and compiler
	$ sudo yum install git cmake cmake-gui gcc-c++

	# umundo required dependencies
	$ sudo yum install avahi-devel dbus-devel protobuf-c-devel protobuf-devel pcre-devel

	# umundo optional dependencies - SWIG and the Java Developer Kit
	$ sudo yum install swig java-openjdk ant

<b>64 bit Note:</b> Same problem as noted above.


### Console / Make

Instructions are a literal copy of building umundo for MacOSX on the console from above:

	$ mkdir -p build/umundo/cli && cd build/umundo/cli
	$ cmake <UMUNDO_SRCDIR>
	[...]
	-- Build files have been written to: .../build/umundo/cli
	$ make

### Eclipse CDT

	$ mkdir -p build/umundo/eclipse && cd build/umundo/eclipse
	$ cmake -G "Eclipse CDT4 - Unix Makefiles" ..
	[...]
	-- Build files have been written to: .../build/umundo/eclipse

Now open Eclipse CDT and import the out-of-source directory as an existing project into workspace, leaving the "Copy projects 
into workspace" checkbox unchecked. There are some more [detailed instruction](http://www.cmake.org/Wiki/Eclipse_CDT4_Generator) available 
in the cmake wiki as well.

### Compiling Dependencies

If the packages in your distribution are too old, you will have to compile current
binaries. This applies especially for SWIG and CMake as they *need* to be rather
current. Have a look at the build dependencies above for minimum versions.

#### SWIG

    $ sudo apt-get install subversion autoconf libpcre3-dev bison wget
    $ wget http://prdownloads.sourceforge.net/swig/swig-2.0.6.tar.gz
    $ tar xvzf swig-2.0.6.tar.gz
    $ cd swig-2.0.6/
    $ ./configure
    $ make
    $ sudo make install
    $ swig -version

This ought to yield version 2.0.5 or higher. 

#### CMake

    $ sudo apt-get remove cmake cmake-data
    $ wget http://www.cmake.org/files/v2.8/cmake-2.8.8.tar.gz
    $ tar xvzf cmake-2.8.8.tar.gz
    $ cd cmake-2.8.8/
    $ ./configure
    $ make
    $ sudo make install
    $ cmake --version

This should say <tt>cmake version 2.8.8</tt>. If you get the bash complaining 
about not finding cmake, logout and login again.

## Windows 7 - Complete Walkthrough

* [<b>Build Reports</b>](http://umundo.tk.informatik.tu-darmstadt.de/cdash/index.php?project=umundo)
* [<b>Precompiled dependencies (32Bit)</b>](https://github.com/tklab-tud/umundo/tree/master/contrib/prebuilt/windows-x86/msvc/lib)
/ [<b>Precompiled dependencies (64Bit)</b>](https://github.com/tklab-tud/umundo/tree/master/contrib/prebuilt/windows-x86_64/msvc/lib)

Building from source on windows is somewhat more involved and instructions are necessarily in prose form.

<b>64 bit Note:</b> The 64Bit version of Windows has not received the proper 
care it needs in the past. I will try to keep it up-to-date if demand 
increases.

### Details on required build-time dependencies

<table>
    <tr><th>Dependency</th><th>Search Path</th><th>CMake Variables</th><th>Comment</th></tr>
	<tr><td bgcolor="#ffd">PCRE<br>[<a href="http://sourceforge.net/projects/gnuwin32/files/pcre/7.0/pcre-7.0.exe/download">download</a>]</td>
		<td>
			<tt>C:/Program Files/GnuWin32/</tt><br>
			<tt>C:/Program Files (x86)/GnuWin32/</tt><br>
		</td><td>
			<tt>PCRE_INCLUDE_DIR</tt> with a full path to the pcre headers<br/>
			<tt>PCRE_PCRE_LIBRARY</tt> with the pcre library<br/>
		</td>
		<td>
			Just download the installer and use the standard installation location or 
			specify the path in the <tt>PCRE_*</tt> variables by hand.
		</td>
	</tr>
	<tr><td bgcolor="#ffd">Protocol Buffers<br>[<a href="http://protobuf.googlecode.com/files/protobuf-2.4.1.zip">download</a>]</td>
		<td>
			<tt>C:/Program Files/GnuWin32/</tt><br>
			<tt>C:/Program Files (x86)/GnuWin32/</tt><br>
		</td><td>
			<tt>PROTOBUF_SRC_ROOT_FOLDER</tt> path to compiled protocol buffers<br/>
		</td><td>
			It is <emph>not</emph> sufficient to download just the precompiled 
			<tt>protoc.exe</tt>. You will need to compile protocol buffers
			for the libraries anyway (see below).
		</td>
	</tr>
</table>

### Prepare compilation

1. Use git to **checkout** the source from <tt>git://github.com/tklab-tud/umundo.git</tt> 
	into any convenient directory. Try not to run CMake with a build directory you
	plan to use until you compiled protobuf. If you did and are having trouble with 
	finding protobuf, just delete the CMake cache and empty the directory to start over.

2. For **PCRE**, you can simply use the prebuilt binary distribution from gnuwin32 
	as linked above. Just download the installer and have him install PCRE into
	the default directory (here <tt>C:\Program Files\GnuWin32</tt>).

3. For **ProtoBuf**, we *need* to build from source for the libraries. Just 
	download and unpack the sources linked from the table above into the same 
	folder as the build directory or one directory above. You can still place them 
	anywhere but then you will have to set <tt>PROTOBUF_SRC_ROOT_FOLDER</tt> for 
	every new build directory you create.
<pre>
build> dir
07/26/2012 06:41 PM    &lt;DIR> umundo
07/26/2012 05:16 PM    &lt;DIR> protobuf–2.4.1 # first option
build> dir ..
07/26/2012 05:16 PM    &lt;DIR> protobuf-2.4.1 # second option
</pre>
	1. Within the ProtoBuf source folder open <tt>vsprojects/protobuf.sln</tt> with
		MS Visual Studio and build everything in Debug and Release configuration via
		<tt>Build->Build Solution</tt>. Just ignore all the errors with regard to 
		<tt>gtest</tt>, the libraries are built nevertheless.
		
	2. You most likely **have to build ProtoBuf twice** for both configurations until you 
		get 5 projects to succeed for both Release and Debug builds.
	
4. Start the **CMake-GUI** and enter the checkout directory in the "Where is the source 
	code" text field. Choose any convenient directory to build the binaries in, but
	try to put it next to the directory with the ProtoBuf build or one level deeper.
	
5. Hit "**Configure**" and choose your toolchain and compiler - I only tested with 
	Visual Studio 10. Hit "Configure" again until there are no more red items in 
	the list. If these instructions are still correct and you did as described 
	above, you should be able to "Generate" the Visual Project Solution.
		
	1. If you have the compiled ProtoBuf libraries installed somewhere else, provide
		the path to the directory in <tt>PROTOBUF_SRC_ROOT_FOLDER</tt> and hit 
		"Configure" again, hoping that he will find all the ProtoBuf libraries.

	2. CMake will still complain about missing the <tt>SWIG_EXECUTABLE</tt> but that 
		is just a friendly reminder and no error at this point.

Now you can generate the MS Visual Studio project file <tt><UMUNDO_SRCDIR>/umundo.sln</tt>. 
Just open it up to continue in your IDE.

<b>Note:</b> We only tested with the MSVC compiler. You can try to compile 
with MinGW but you would have to build all the dependent libraries as well. 
I did not manage to compile ZeroMQ the last time I tried.

### Language Bindings

The process described above will let you compile the umundo libraries. If you 
want some language bindings into Java or C#, you will have to install some more
packages:

<table>
  <tr><th>Dependency</th><th>Search Path</th><th>CMake Variables</th><th>Comment</th></tr>
	<tr><td bgcolor="#dfd">SWIG<br>[<a href="http://prdownloads.sourceforge.net/swig/swigwin-2.0.7.zip">download</a>]</td>
		<td>
			<tt>C:/Program Files/swig/</tt><br>
			<tt>C:/Program Files (x86)/swig/</tt><br>
			Every entry in <tt>%ENV{PATH}</tt>
		</td><td>
			<tt>SWIG_EXECUTABLE</tt> with a full path to swig.exe<br/>
		</td>
		<td>
			Just download the archive and extract it into the search path (make 
			sure to remove an eventual version suffix from the directory name)
			If CMake cannot find <tt>swig.exe</tt>, you can always specify a path in 
			<tt>SWIG_EXECUTABLE</tt> per hand.
		</td>
	</tr>
	<tr><td bgcolor="#dfd">Java SDK<br>[<a href="http://www.oracle.com/technetwork/java/javase/downloads/index.html">download</a>]</td>
		<td>
			<tt>%ENV{JAVA_HOME}</tt><br>
			<a href="http://cmake.org/gitweb?p=cmake.git;a=blob_plain;f=Modules/FindJNI.cmake;hb=HEAD">FindJNI.cmake</a>
			
		</td><td>
			<tt>JAVA_*</tt>
		</td><td>
			The linked FindJNI.cmake is the actual module used by CMake to find a
			Java installation with for JNI headers on the system. I it fails, just 
			set the <tt>%JAVA_HOME</tt> environment variable to the root of your JDK.
		</td>
	</tr>
	<tr><td bgcolor="#dfd">Ant<br>[<a href="http://ant.apache.org/bindownload.cgi">download</a>]</td>
		<td>
			<tt>%ENV{ANT_HOME}</tt><br>
			Every entry in <tt>%ENV{PATH}</tt>
		</td><td>
			<tt>ANT_EXECUTABLE</tt>
		</td><td>
			Just download a binary release, copy it somewhere and export <tt>%ANT_HOME</tt>
			as the full path to your ant root directory. Alternatively, set the CMake
			variable every time.
		</td>
	</tr>
	<tr><td bgcolor="#dfd">MS .NET Framework<br>[<a href="http://www.microsoft.com/net/download">download</a>]</td>
		<td>
			<tt>C:/Windows/Microsoft.NET/Framework/v3.5</tt><br>
			<tt>C:/Windows/Microsoft.NET/Framework/v4.0</tt><br>
			Every entry in <tt>%PATH</tt>
		</td><td>
			<tt>CSC_EXECUTABLE</tt> with a full path to csc.exe<br/>
		</td>
		<td>
			Just use the Microsoft installer. I am not even sure whether you actually
			change the installation directory. If you have problems, just provide the 
			full path to the C# compiler in <tt>CSC_EXECUTABLE</tt>.
		</td>
	</tr>
</table>

You will need SWIG in any case. If you only want Java, you would not need to 
install the .NET Framework and the other way around. If you installed the 
packages as outlined in the table above, CMake will create the following
libraries:

#### Java
There are only two files of interest built for Java:
<pre>
build\umundo>ls lib
umundocore.jar        # The Java archive
umundocoreJava.dll    # The JNI library for System.load()
</pre>

The Java archive contains generated wrappers for the umundo.core C++ code in 
umundocoreJava.dll and hand-written implementations of the layers on top. See the 
[Eclipse sample project](https://github.com/tklab-tud/umundo/tree/master/contrib/samples/java)
to get an idea on how to use the API.

#### CSharp
There are again, only two files of interest for C# development
<pre>
build\umundo>ls lib
umundoCSharp.dll     # The managed code part
umundocoreCSharp.dll # The native C++ code used via DLLInvoke
</pre>

Here again, the umundoCSharp.dll contains all generated wrappers for the umundo.core
C++ code in umundocoreCSharp.dll. Other C# functionality for the umundo layers 
on top will also eventually find its way into this dll. There is also a sample
[Visual Studio solution](https://github.com/tklab-tud/umundo/tree/master/contrib/samples/csharp), 
illustrating how to use umundo.core with C#.

## Cross Compiling

Cross compiling for Android and iOS is best done with the <tt>build-umundo-*</tt> scripts in <tt>contrib</tt>. You have to make 
sure that CMake can find <tt>protoc-umundo-cpp-rpc</tt> and <tt>protoc-umundo-java-rpc</tt> on your system, both can be build
and installed with a host-native (non cross-compiled) installation first.

Cross Compiling for Android on Windows is possible but the process is not wrapped in a script yet. Have a look at the unix shell 
scripts to see what's needed.

# Build process

We are using CMake to build uMundo for every platform. When <tt>cmake</tt> is invoked, it will look for a <tt>CMakeLists.txt</tt>
file in the given directory and prepare a build in the current directory. CMake itself can be considered as a meta build-system as
it will only generate the artifacts required for an actual build-system. The default is to generate files for <tt>make</tt> on 
unices and files for <tt>nmake</tt> on windows. If you invoke <tt>ccmake</tt> instead of <tt>cmake</tt>, you get an user interfaces
to set some variables related to the build:

### What to build

<dt><b>CMAKE_BUILD_TYPE</b></dt>
<dd>Only <tt>Debug</tt> and <tt>Release</tt> are actually supported. In debug builds, all asserts are stripped and the default
	log-level is decreased.</dd>

<dt><b>BUILD_PREFER_STATIC_LIBRARIES</b></dt>
<dd>Prefer static libraries in <tt>contrib/prebuilt/</tt> or system supplied libraries as found by CMake.</dd>

<dt><b>BUILD_STATIC_LIBRARIES</b></dt>
<dd>Create the uMundo libraries as static libraries. This does not apply to the JNI library which needs to be a shared library for
	Java.</dd>

<dt><b>BUILD_TESTING</b></dt>
<dd>Build the test executables.</dd>

<dt><b>BUILD_UMUNDO_APPS</b></dt>
<dd>Include the <tt>apps/</tt> directory when building.</dd>

<dt><b>BUILD_UMUNDO_RPC</b></dt>
<dd>Build the <tt>umundorpc</tt> library for remote procedure calls via uMundo.</dd>

<dt><b>BUILD_UMUNDO_S11N</b></dt>
<dd>Build the <tt>umundos11n</tt> library for object serialization. Only Googles ProtoBuf is supported as o now.</dd>

<dt><b>BUILD_UMUNDO_UTIL</b></dt>
<dd>Build <tt>umundoutil</tt> with some growing set of convenience services.</dd>

<dt><b>DIST_PREPARE</b></dt>
<dd>Put all libraries and binaries into SOURCE_DIR/package/ to prepare a release. We need access to all artifacts from other 
	platforms to create the installers with platform independent JARs and cross-compiled mobile platforms.</dd>

### Implementations

<dt><b>DISC_AVAHI</b></dt>
<dd>Use the Avahi ZeroConf implementation with umundocore found on modern Linux distributions.</dd>

<dt><b>DISC_BONJOUR</b></dt>
<dd>Use the Bonjour ZeroConf implementation found on every MacOSX installation and every iOS device.</dd>

<dt><b>DISC_BONJOUR_EMBED</b></dt>
<dd>Embed the Bonjour ZeroConf implementation into umundocore. This is the default for Android and Windows.</dd>

<dt><b>NET_ZEROMQ</b></dt>
<dd>Use ZeroMQ to connect nodes to each other and publishers to subscribers.</dd>

<dt><b>NET_ZEROMQ_RCV_HWM, NET_ZEROMQ_SND_HWM</b></dt>
<dd>High water mark for ZeroMQ queues in messages. One uMundo message represents multiple ZeroMQ messages, one per meta field and 
	one for the actual data.</dd>

<dt><b>S11N_PROTOBUF</b></dt>
<dd>Use Google's ProtoBuf to serialize objects.</dd>

<dt><b>RPC_PROTOBUF</b></dt>
<dd>Use Google's ProtoBuf to call remote methods.</dd>

### CMake files

Throughout the source, there are <tt>CMakeLists.txt</tt> build files for CMake. The topmost build file will call the build files
from the directories directly contained via <tt>add_directory</tt>, which in turn call build files further down the directory 
structure.

    uMundo
     |-CMakeLists.txt 
     |          Uppermost CMakeLists.txt to setup the project with all variables listed above.
     |          Includes contrib/cmake/ as the module path for CMake modules.
     |          Defines where built files will end up.
     |          Configures additional search paths for libraries and executables.
     |          Sets global compiler flags.
     |          Uses ant to build the JAR for Java.
     |
     |-apps/CMakeLists.txt
     |          Invokes CMakeLists.txt in the sub-directories to build all apps if their dependencies are met. 
     |
     |-core/CMakeLists.txt
     |-s11n/CMakeLists.txt
     |-rpc/CMakeLists.txt
     |          Gather all source files for the respective component into a library.
     |          Find and link all required libraries either as prebuilts or system supplied libraries.
     |          Call INSTALL* CMake macros to register files for install and package targets.
     |
     |-core/bindings/CMakeLists.txt
     |          Find SWIG to build the bindings.
     |
     |-core/bindings/java/CMakeLists.txt
     |          Find the JNI libraries and use SWIG to build the Java wrappers.
