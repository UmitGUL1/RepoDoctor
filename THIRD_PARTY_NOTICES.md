# Third-party notices

RepoDoctor's own source is MIT licensed (see LICENSE).

- **nlohmann/json 3.11.3** is MIT licensed. Its original source notices and full
  license are retained in `third_party/nlohmann/LICENSE.MIT`.
- The Windows desktop package dynamically links **Qt 6.8.3** open-source
  libraries. Qt remains separately licensed under the GNU LGPL version 3
  (or other terms stated in the individual Qt components). LGPL-3.0 and
  GPL-3.0 texts accompany the package in `licenses/`.
  Unmodified Qt source is available from
  https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz
  and https://code.qt.io/.
  Qt libraries and plugins are separate replaceable DLLs; keep a compatible
  ABI when replacing them. Reverse engineering for debugging modifications
  to those LGPL libraries is permitted as required by that license.
- The packaged Microsoft Visual C++ runtime is distributed under Microsoft's
  redistributable runtime terms. See https://visualstudio.microsoft.com/license-terms/.

GoogleTest is used only by test executables; it is not included in the application
package. Linux uses the distribution's libgit2 and Qt packages with their own
copyright and licensing notices.
