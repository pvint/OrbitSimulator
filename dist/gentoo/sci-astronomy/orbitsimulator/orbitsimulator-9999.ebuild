# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake xdg

DESCRIPTION="Rocket launch and orbit simulator for building intuition about orbital mechanics"
HOMEPAGE="https://github.com/pvint/OrbitSimulator"

if [[ ${PV} == *9999 ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/pvint/OrbitSimulator.git"
else
	SRC_URI="https://github.com/pvint/OrbitSimulator/archive/v${PV}.tar.gz -> ${P}.tar.gz"
	S="${WORKDIR}/OrbitSimulator-${PV}"
	KEYWORDS="~amd64"
fi

# The Earth and Moon textures are NASA imagery, in the public domain.
LICENSE="GPL-3+ public-domain"
SLOT="0"
IUSE="+gui test"
RESTRICT="!test? ( test )"

RDEPEND="
	gui? (
		dev-qt/qtbase:6[concurrent,gui,widgets]
		dev-qt/qtdeclarative:6
		dev-qt/qtquick3d:6
	)
"
DEPEND="
	${RDEPEND}
	dev-cpp/nlohmann_json
	test? ( >=dev-cpp/catch-3:0 )
"

src_configure() {
	local mycmakeargs=(
		-DORBIT_BUILD_APP=$(usex gui)
		-DORBIT_BUILD_TESTS=$(usex test)
		-DORBIT_DEPLOY_QT=OFF
		# Use the system's nlohmann_json and Catch2; never download.
		-DFETCHCONTENT_FULLY_DISCONNECTED=ON
	)
	cmake_src_configure
}

src_test() {
	# The GUI tests need no display.
	QT_QPA_PLATFORM=offscreen cmake_src_test
}
