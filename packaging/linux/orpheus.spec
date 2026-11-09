# Paquet RPM d'Orpheus (Fedora). Construit par .github/workflows/installers.yml :
#   rpmbuild -ba -D "orpheus_version 1.2.0" packaging/linux/orpheus.spec
# avec l'archive des sources orpheus-<version>.tar.gz dans le dossier SOURCES.
%{!?orpheus_version: %global orpheus_version 1.2.0}
%bcond_without tests

Name:           orpheus
Version:        %{orpheus_version}
Release:        1%{?dist}
Summary:        Lecteur de musique : YouTube, Spotify et cache local
# Aucune licence n'est encore indiquée dans le dépôt.
License:        LicenseRef-Unspecified
URL:            https://github.com/killex8569/Orpheus
Source0:        orpheus-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Core) >= 6.5
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Sql)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Test)
BuildRequires:  pkgconfig(mpv)
BuildRequires:  desktop-file-utils

# Chargés à l'exécution, donc invisibles pour la détection automatique des dépendances :
# icônes SVG, base SQLite, modules QML (contrôles, mises en page, dialogues).
Requires:       qt6-qtsvg
Requires:       qt6-qtdeclarative
# Enregistrement des morceaux par yt-dlp (ffmpeg-free de Fedora ou ffmpeg de RPM Fusion).
Requires:       /usr/bin/ffmpeg
# Copie de yt-dlp gérée par Orpheus (archive Python), téléchargée avec curl.
Requires:       python3
Requires:       curl
# Moteur JavaScript demandé par YouTube (deno n'est pas dans Fedora).
Recommends:     nodejs
Recommends:     yt-dlp

%description
Orpheus est un lecteur de musique de bureau en C++/Qt 6, avec lecture par libmpv.
Il cherche sur YouTube ou Spotify, garde chaque morceau écouté dans un cache local
(lu ensuite depuis le disque), importe des playlists YouTube et Spotify, affiche les
paroles synchronisées, tient des statistiques d'écoute et permet l'écoute partagée
(Jam) sur le réseau local.

%prep
%autosetup -n orpheus-%{version}

%build
%cmake -G Ninja -DORPHEUS_PACKAGE=rpm
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/orpheus.desktop
%if %{with tests}
%{_vpath_builddir}/orpheus_tests -silent
%endif

%files
%doc README.md
%{_bindir}/orpheus
%{_datadir}/applications/orpheus.desktop
%{_datadir}/icons/hicolor/scalable/apps/orpheus.svg

%changelog
* Mon Nov 09 2026 killex8569 - 1.2.0-1
- Version Android (APK) ; correction d'un plantage à la fermeture pendant un
  Jam et de plusieurs bugs (mise à jour de yt-dlp sans curl, lecture d'une
  file remplie sans morceau en cours, fuites mémoire)

* Tue Nov 03 2026 killex8569 - 1.1.2-1
- Compte Spotify gratuit (session du navigateur), choix des playlists à
  importer, téléchargement et dossier au moment de l'import

* Mon Nov 02 2026 killex8569 - 1.1.1-1
- Premier paquet RPM : rejoindre un Jam par son nom, dossier par playlist,
  ordre des playlists, correction des doublons dans le Jam
