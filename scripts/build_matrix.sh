#!/usr/bin/env bash

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(dirname -- "$script_dir")

cd "$project_dir"
unset LD_PRELOAD

matrix_dir="build/matrix"
log_dir="$matrix_dir/logs"
summary_file="$matrix_dir/summary.txt"

mkdir -p "$log_dir"
: >"$summary_file"

builds=0
failures=0

for preset in debug relWithDebInfo release
do
    if [ "$preset" = "debug" ]
    then
        asan=ON
        ubsan=ON
    else
        asan=OFF
        ubsan=OFF
    fi

    for render in ON OFF
    do
        for tracy in ON OFF
        do
            for testing in ON OFF
            do
                name="${preset}-render-${render}-tracy-${tracy}-tests-${testing}"
                build_dir="$matrix_dir/$name"
                log_file="$log_dir/$name.log"

                builds=$((builds + 1))
                result=PASS

                printf '\n[%d/24] %s\n' "$builds" "$name"
                : >"$log_file"

                if ! cmake --preset "$preset" \
                    --fresh \
                    -B "$build_dir" \
                    -DSIMNET_WARNINGS_AS_ERRORS=ON \
                    -DSIMNET_ENABLE_ASAN="$asan" \
                    -DSIMNET_ENABLE_UBSAN="$ubsan" \
                    -DSIMNET_ENABLE_RENDER="$render" \
                    -DSIMNET_ENABLE_TRACY="$tracy" \
                    -DBUILD_TESTING="$testing" \
                    >>"$log_file" 2>&1
                then
                    result="CONFIGURE FAILED"
                elif ! cmake --build "$build_dir" --parallel \
                    >>"$log_file" 2>&1
                then
                    result="BUILD FAILED"
                elif [ "$testing" = "ON" ] \
                    && ! ctest --test-dir "$build_dir" --output-on-failure \
                    >>"$log_file" 2>&1
                then
                    result="TESTS FAILED"
                fi

                if [ "$result" != PASS ]
                then
                    failures=$((failures + 1))
                fi

                printf '%-16s %s\n' "$result" "$name" | tee -a "$summary_file"
            done
        done
    done
done

printf '\nCompleted %d configurations with %d failures\n' "$builds" "$failures"
printf 'Summary: %s\n' "$summary_file"
printf 'Logs: %s\n' "$log_dir"

if [ "$failures" -ne 0 ]
then
    exit 1
fi