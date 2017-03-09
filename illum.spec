Name:           illum
Version:        0.3
Release:        1%{?dist}
Summary:        A daemon that responds to brightness keys by changing the backlight level

License:        AGPLv3
URL:            https://github.com/jmesmon/illum
%global commit 658765ba3df3c27a3f4df5728605d105e170a264
%global ccan_commit 3ed55657aa0208046cdb5c57cc9dbb2e6300a97b
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global owner jmesmon
Source0:        https://github.com/%{owner}/%{name}/archive/%{commit}/%{name}-%{commit}.tar.gz
Source1:        https://github.com/%{owner}/ccan/archive/%{ccan_commit}/ccan-%{ccan_commit}.tar.gz

BuildRequires:  ninja
BuildRequires:  pkg-config
BuildRequires:  libev-devel
BuildRequires:  libevdev-devel

%description

%prep
%setup -qn %{name}-%{commit}
tar -zxf %{SOURCE1}
rmdir ccan
mv $(basename %{SOURCE1} .tar.gz) ccan

%build
./build

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT PREFIX=/usr ./do-install

%files
%defattr(-,root,root)
%doc README
%{_bindir}/%{name}-d
/usr/lib/systemd/system/%{name}.service

%changelog
