CPMAddPackage(
    NAME fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
    URL_HASH SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS "FMT_INSTALL ON" "FMT_TEST OFF"
)
