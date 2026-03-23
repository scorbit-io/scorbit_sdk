{ pkgs, lib, ... }:
let
  commands = rec {
    generate = "cmake -S . -B build -G Ninja -DBUILD_EXAMPLES=ON -DFORCE_COLORED_OUTPUT=ON \${1}";
    build =
      {
        genArgs ? "",
      }:
      ''
        ${generate} ${genArgs} ''${1}
        cmake --build build
      '';
    run = "./build/\${1}";

    down = "docker compose down -v --remove-orphans";
    up = "docker compose up -d --build --wait";
  };
in
{
  env = lib.optionalAttrs pkgs.stdenv.isDarwin {
    LLDB_DEBUGSERVER_PATH = "/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Resources/debugserver";
  };

  packages = with pkgs; [
    boost
    openssl
    nlohmann_json

    cmake
    ninja

    #optional
    clang-tools
    fblog
  ];

  scripts = {
    gen.exec = commands.generate;
    build.exec = commands.build { };
    run.exec = commands.run;

    down.exec = commands.down;
    up.exec = commands.up;
    token.exec = "docker run --rm -it --init scorb-token";
    restart-centrifugo.exec = ''
      docker compose down centrifugo
      sleep 3
      docker compose up -d centrifugo
    '';
  };

  tasks = {
    "app:build-dbg" = {
      exec = commands.build { genArgs = "-DCMAKE_BUILD_TYPE=Debug"; };
    };
  };
}
