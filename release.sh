#!/bin/bash
#
# release.sh - rilascia GrADS <versione>-<N>
#
#   ./release.sh <N>     rilascia la release N
#   ./release.sh -n <N>  solo i controlli, non modifica nulla
#   ./release.sh -c      controlla solo che il tarball scaricato da Source0
#                        sia uguale al commit corrente
#
# Prima di lanciarlo vanno scritte a mano le voci della release in:
#   - %changelog di grads.spec e di opengrads.spec  ("* ... - <versione>-<N>")
#   - ChangeLog                                     ("release <versione>-<N>")
#   - changelog_<versione>.txt                      (solo avviso se manca)
#
# Lo script:
#   1. imposta "Release:" nei due spec e la release in readme_dvd.txt
#   2. fa il commit "release <versione>-<N>" di tutti i file tracciati modificati
#   3. fa il push del branch
#   4. scarica il tarball da Source0 e lo confronta con il commit corrente
#
# Non crea tag: i tag si mettono a mano solo ai cambi di versione (es. 2.2.6).
# Gli spec scaricano i sorgenti dalla punta del branch che si chiama come la
# versione (Source0), quindi quello che non e' nel push non finisce nell'RPM.
# Con lo stesso N di prima (RPM ricostruito con lo stesso numero) lo script
# fa solo commit, push e controllo.
#
# Il remote e' "origin"; si cambia con REMOTE=<nome> ./release.sh ...

set -euo pipefail

REMOTE=${REMOTE:-origin}
SPECS="grads.spec opengrads.spec"

die()   { echo "ERRORE: $*" >&2; exit 1; }
info()  { echo "==> $*"; }
usage() { sed -n '3,8p' "$0"; exit 2; }

dry=0; checkonly=0
while getopts "nc" opt; do
  case $opt in
    n) dry=1 ;;
    c) checkonly=1 ;;
    *) usage ;;
  esac
done
shift $((OPTIND-1))

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

VERSION=$(sed -n 's/^Version: *//p' grads.spec)
[ -n "$VERSION" ] || die "Version non trovata in grads.spec"
VRE=${VERSION//./\\.}
URL=$(sed -n 's/^Source0: *//p' grads.spec | sed "s/%{version}/$VERSION/g")
BRANCH=$(git symbolic-ref -q --short HEAD) || die "HEAD staccata: passare al branch $VERSION"
[ "$BRANCH" = "$VERSION" ] \
  || die "sei sul branch '$BRANCH', ma Source0 scarica il branch '$VERSION'"

# Scarica il tarball da Source0 e lo confronta con HEAD
check_tarball() {
  local tmp rc i
  tmp=$(mktemp -d)
  mkdir "$tmp/dl" "$tmp/git"
  git archive --prefix="GrADS-$VERSION/" HEAD | tar x -C "$tmp/git"
  info "scarico $URL"
  # dopo un push GitHub puo' servire ancora il tarball precedente per qualche secondo
  rc=1
  for i in 1 2 3 4 5 6; do
    rm -rf "$tmp/dl"; mkdir "$tmp/dl"
    if curl -fsSL -o "$tmp/dl.tar.gz" "$URL" && tar xzf "$tmp/dl.tar.gz" -C "$tmp/dl" \
       && diff -rq "$tmp/dl" "$tmp/git" >/dev/null; then rc=0; break; fi
    sleep 5
  done
  if [ $rc -eq 0 ]; then
    rm -rf "$tmp"
    info "il tarball corrisponde a HEAD ($(git rev-parse --short HEAD))"
  else
    diff -rq "$tmp/dl" "$tmp/git" 2>&1 | head -20 >&2 || true
    rm -rf "$tmp"
    die "il tarball scaricato e' diverso da HEAD: push mancante? (riprovare con ./release.sh -c)"
  fi
}

if [ $checkonly -eq 1 ]; then
  [ $# -eq 0 ] || usage
  [ -z "$(git status --porcelain --untracked-files=no)" ] \
    || echo "AVVISO: ci sono modifiche non committate, che non sono nel tarball"
  check_tarball
  exit 0
fi

[ $# -eq 1 ] || usage
N=$1
[[ $N =~ ^[1-9][0-9]*$ ]] || die "la release deve essere un intero positivo: '$N'"

# --- controlli, prima di toccare qualsiasi cosa ---

err=0
for s in $SPECS; do
  grep -q '^Release: *[0-9]\+%{?dist}$' "$s" || { echo "riga 'Release:' non riconosciuta in $s" >&2; err=1; }
  grep -q "^\* .* - $VRE-$N\$" "$s"          || { echo "manca la voce '$VERSION-$N' nel %changelog di $s" >&2; err=1; }
done
grep -q "release $VRE-$N\b" ChangeLog        || { echo "manca 'release $VERSION-$N' in ChangeLog" >&2; err=1; }
[ $err -eq 0 ] || die "scrivere le voci mancanti e rilanciare"
grep -q "release $VRE-$N\b" "changelog_$VERSION.txt" 2>/dev/null \
  || echo "AVVISO: 'release $VERSION-$N' non compare in changelog_$VERSION.txt"

CUR=$(sed -n 's/^Release: *\([0-9]\+\).*/\1/p' grads.spec)
[ "$N" -ge "$CUR" ] || die "la release corrente e' $CUR: $N e' piu' bassa"

git fetch -q "$REMOTE" "$BRANCH"
git merge-base --is-ancestor FETCH_HEAD HEAD \
  || die "$BRANCH non contiene $REMOTE/$BRANCH: allineare il branch prima di rilasciare"

if [ $dry -eq 1 ]; then
  info "controlli superati per $VERSION-$N (release corrente: $CUR)"
  exit 0
fi

# --- modifiche ---

for s in $SPECS; do
  sed -i "s/^\(Release: *\)[0-9]\+%{?dist}\$/\1$N%{?dist}/" "$s"
done
# readme_dvd.txt: nome dell'RPM e "la release (qui N)"
sed -i -e "s/$VRE-[0-9]\+/$VERSION-$N/g" -e "s/(qui [0-9]\+)/(qui $N)/" readme_dvd.txt

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  info "file nel commit 'release $VERSION-$N':"
  git status --short --untracked-files=no
  read -r -p "Procedo con commit e push su $REMOTE/$BRANCH? [s/N] " ans
  [[ $ans =~ ^[sSyY]$ ]] || die "annullato (le modifiche ai file restano nell'albero di lavoro)"
  git commit -q -a -m "release $VERSION-$N"
else
  info "nessun file modificato: push del commit corrente $(git rev-parse --short HEAD)"
fi

git push -q "$REMOTE" "$BRANCH"
info "push su $REMOTE/$BRANCH fatto ($(git rev-parse --short HEAD))"

check_tarball
info "release $VERSION-$N pronta. Su ogni host: spectool -g -f -R opengrads.spec, poi rpmbuild"
