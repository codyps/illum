Name:           illum
Version:        0.3
Release:        1%{?dist}
Summary:        A daemon that responds to brightness keys by changing the backlight level

License:        AGPLv3
URL:            https://github.com/jmesmon/illum
%global commit 658765ba3df3c27a3f4df5728605d105e170a264
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global owner jmesmon
Source0:        https://github.com/%{owner}/%{name}/archive/%{commit}/%{name}-%{commit}.tar.gz

BuildRequires:  ninja
BuildRequires:  pkg-config
BuildRequires:  sh
Requires:       libev
Requires:       libevdev

%description

%prep
%setup -qn %{name}-%{commit}

%build
./build

%install
rm -rf $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT PREFIX=/usr ./do-install

%files
%doc

%changelog
