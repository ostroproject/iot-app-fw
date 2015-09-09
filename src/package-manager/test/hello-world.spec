Summary: IoT Application Framework test package
Name: hello-world
Version: 1.0.0
Release: 1
License: BSD
Group: Test
URL: http://github.com/otcshare/iot-app-fw
Source0: %{name}-%{version}.tar.gz

%define homedir %{getenv:HOME}
%define user %{getenv:USER}

%description
This is a test package for verifying iotpm functionality

%prep
%setup -q

%build
make

%install
rm -rf $RPM_BUILD_ROOT
%make_install

%files
%defattr(-,%{user},%{user},-)
%{homedir}
