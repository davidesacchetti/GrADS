% installare tutte le dipendenze presenti su ../GRADS-DEPENDENCIES (vedi i vari readme_dvd.txt)
% gadap-2.0.2  libsx-2.08  libXaw3dXft-1.6.4 are needed

# cd ..
# rm /root/rpmbuild/SOURCES/*
# tar -cvzf /root/rpmbuild/SOURCES/v2.2.5.tar.gz GrADS-2.2.5

# dnf install Xaw3d-devel cairo-devel g2clib-devel hdf hdf-devel hdf5-devel jasper-devel libXaw-devel libgeotiff-devel ncurses-devel netcdf-devel readline-devel shapelib-devel udunits2-devel

# rpmbuild -ba opengrads.spec --define "__install_path /opt/grads" --define "module_dir /usr/share/Modules/modulefiles"

dnf install /root/rpmbuild/RPMS/x86_64/grads-2.2.4-0.el8.x86_64.rpm

% gradspy sta in /opt/grads/lib/libgradspy.so
per mettere gradspy nel venv fare cosi':
mkdir /tmp/gradspy-build
# per usare gradspy originale della versione di grads
cp /opt/other/grads-2.2.5/grads-2.2.5/src/{gradspy.c,gradspy.h,setup.py} /tmp/gradspy-build/
pip install --no-build-isolation /tmp/gradspy-build
# per usare gradspy con la versione di davide (ATTENZIONE: bidogna prima attivare il venv)
pip install --no-build-isolation /opt/other/gradspy
