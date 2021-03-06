//
//  libavg - Media Playback Engine. 
//  Copyright (C) 2003-2014 Ulrich von Zadow
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//  Current versions can be found at www.libavg.de
//

#ifndef _ImagingProjection_H_
#define _ImagingProjection_H_

#include "../api.h"

#include "../base/Rect.h"
#include "Pixel32.h"
#include "GLShaderParam.h"

#include <boost/shared_ptr.hpp>

namespace avg {

class OGLShader;
typedef boost::shared_ptr<OGLShader> OGLShaderPtr;
class VertexArray;
typedef boost::shared_ptr<VertexArray> VertexArrayPtr;

class AVG_API ImagingProjection
{
public:
    ImagingProjection(IntPoint size);
    ImagingProjection(IntPoint srcSize, IntRect destRect);
    virtual ~ImagingProjection();

    void setColor(const Pixel32& color);
    void draw(const OGLShaderPtr& pShader);

private:
    void init(IntPoint srcSize, IntRect destRect);

    IntPoint m_SrcSize;
    IntRect m_DestRect;
    IntPoint m_Offset;
    Pixel32 m_Color;
    VertexArrayPtr m_pVA;
    Mat4fGLShaderParamPtr m_pTransformParam;
    glm::mat4 m_ProjMat;
};

typedef boost::shared_ptr<ImagingProjection> ImagingProjectionPtr;

}


#endif 


