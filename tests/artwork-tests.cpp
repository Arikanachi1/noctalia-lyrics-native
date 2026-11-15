#include "artwork.hpp"
#include <QtCore>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir temp;int failures=0;
    auto check=[&](bool ok,const char *name){if(!ok){std::cerr<<name<<" FAILED\n";++failures;}};
    QFile image(temp.filePath("cover.png"));if(!image.open(QIODevice::WriteOnly))return 2;
    image.write("fixture");image.close();
    Artwork art(temp.path());
    art.setSource(QUrl::fromLocalFile(image.fileName()).toString());
    check(art.path()==image.fileName(),"local cover resolves");
    art.setSource("file:///definitely/missing/cover.png");check(art.path().isEmpty(),"missing cover clears old image");
    art.setSource("data:image/png;base64,invalid");check(art.path().isEmpty(),"unsupported source remains empty");
    art.setSource(QUrl::fromLocalFile(image.fileName()).toString());art.setSource("");
    check(art.path().isEmpty(),"track without art never inherits cover");
    return failures?1:0;
}
