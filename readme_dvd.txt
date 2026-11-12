% installare tutte le dipendenze presenti su ../GRADS-DEPENDENCIES (vedi i vari readme_dvd.txt)
% gadap-2.0.2  libsx-2.08  libXaw3dXft-1.6.4 are needed

% la directory del progetto deve chiamarsi GrADS-<versione> (gli spec fanno %setup -n GrADS-%{version})
# cd ..
# rm /root/rpmbuild/SOURCES/*
# tar --exclude=.git -cvzf /root/rpmbuild/SOURCES/v2.2.5.tar.gz GrADS-2.2.5

# dnf install Xaw3d-devel cairo-devel g2clib-devel hdf hdf-devel hdf5-devel jasper-devel libXaw-devel libgeotiff-devel ncurses-devel netcdf-devel readline-devel shapelib-devel udunits2-devel

# rpmbuild -ba GrADS-2.2.5/opengrads.spec --define "__install_path /opt/grads" --define "module_dir /usr/share/Modules/modulefiles"

% la release (qui 6) e' quella di "Release:" in opengrads.spec; il suffisso e' .el8 su Rocky 8, .fc44 su Fedora 44
# dnf install /root/rpmbuild/RPMS/x86_64/grads-2.2.5-6.*.x86_64.rpm

% gradspy sta in /opt/grads/lib/libgradspy.so (variabile GAGPY, impostata da "module load grads")
% per mettere gradspy nel venv (ATTENZIONE: bisogna prima attivare il venv, altrimenti finisce in ~/.local):
pip install numpy setuptools
# per usare gradspy originale della versione di grads
mkdir -p /tmp/gradspy-build
cp /opt/other/GrADS-2.2.5/src/{gradspy.c,gradspy.h,setup.py} /tmp/gradspy-build/
pip install --no-build-isolation /tmp/gradspy-build
# per usare gradspy con la versione di davide
pip install --no-build-isolation /opt/other/gradspy
% verifica: deve stampare un percorso dentro il venv
python -c "import gradspy; print(gradspy.__file__)"
