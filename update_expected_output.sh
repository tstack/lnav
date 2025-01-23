#!/usr/bin/env bash

srcdir="$1"
builddir="$2"

expected_dir="$1/expected"
expected_am="${expected_dir}/Makefile.am"

mkdir -p "${expected_dir}"

for fname in $(ls -t ${builddir}/*.cmd); do
  echo
  echo "Checking test ${fname}:"
  echo -n "  "
  cat "${fname}"
  stem=$(echo $fname | sed -e 's/.cmd$//')
  exp_stem="${srcdir}/expected/$(basename $stem)"

  echo "    $(basename "$stem").out \\" >> "${expected_am}.tmp"
  echo "    $(basename "$stem").err \\" >> "${expected_am}.tmp"

  if ! test -f "${exp_stem}.out"; then
    printf '\033[0;32mBEGIN\033[0m %s.out\n' "${stem}"
    cat "${stem}.out"
    printf '\033[0;32mEND\033[0m   %s.out\n' "${stem}"
    if test x"${AUTO_APPROVE}" = x""; then
      echo "Expected stdout is missing, update with the above?"
      select yn in "Yes" "No"; do
        case $yn in
          Yes ) cp "${stem}.out" "${exp_stem}.out"; break;;
          No ) exit;;
        esac
      done
    else
      cp "${stem}.out" "${exp_stem}.out"
    fi
  else
    if ! cmp "${exp_stem}.out" "${stem}.out"; then
      diff --color=always -u "${exp_stem}.out" "${stem}.out"
      if test x"${AUTO_APPROVE}" = x""; then
        echo "Expected stdout is different, update with the above?"
        select yn in "Yes" "No"; do
          case $yn in
            Yes ) cp "${stem}.out" "${exp_stem}.out"; break;;
            No ) exit;;
          esac
        done
      else
        cp "${stem}.out" "${exp_stem}.out"
      fi
    fi
  fi

  if ! test -f "${exp_stem}.err"; then
    printf '\033[0;31mBEGIN\033[0m %s.err\n' "${stem}"
    cat "${stem}.err"
    printf '\033[0;31mEND\033[0m   %s.err\n' "${stem}"
    if test x"${AUTO_APPROVE}" = x""; then
      echo "Expected stderr is missing, update with the above?"
      select yn in "Yes" "No"; do
        case $yn in
          Yes ) cp "${stem}.err" "${exp_stem}.err"; break;;
          No ) exit;;
        esac
      done
    else
      cp "${stem}.err" "${exp_stem}.err"
    fi
  else
    if ! cmp "${exp_stem}.err" "${stem}.err"; then
      diff --color=always -u "${exp_stem}.err" "${stem}.err"
      if test x"${AUTO_APPROVE}" = x""; then
        echo "Expected stderr is different, update with the above?"
        select yn in "Yes" "No"; do
          case $yn in
            Yes ) cp "${stem}.err" "${exp_stem}.err"; break;;
            No ) exit;;
          esac
        done
      else
        cp "${stem}.err" "${exp_stem}.err"
      fi
    fi
  fi
done

cat > "${expected_am}.new" <<EOF

dist_noinst_DATA = \\
$(sort "${expected_am}.tmp")
    \$()
EOF

if ! cmp "${expected_am}" "${expected_am}.new"; then
  cp "${expected_am}.new" "${expected_am}"
fi

rm "${expected_am}.new"
rm "${expected_am}.tmp"
