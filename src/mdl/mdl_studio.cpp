#include "lang.h"
#include "util.h"
#include "log.h"
#include "mdl_studio.h"
#include "Settings.h"
#include "Renderer.h"
#include "forcecrc32.h"

void StudioModel::CalcBoneAdj()
{
	// Valve
	int					i, j;
	float				value;
	mstudiobonecontroller_t* pbonecontroller;

	pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	for (j = 0; j < m_pstudiohdr->numbonecontrollers; j++)
	{
		i = pbonecontroller[j].index;
		if (i < 4)
		{
			// check for 360% wrapping
			if (pbonecontroller[j].type & STUDIO_RLOOP)
			{
				value = m_controller[i] * (360.0f / 256.0f) + pbonecontroller[j].start;
			}
			else
			{
				value = m_controller[i] / 255.0f;
				if (value < 0.0f) value = 0.0f;
				if (value > 1.0f) value = 1.0f;
				value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
			// print_log("{} {} {} : {}\n", m_controller[j], m_prevcontroller[j], value, dadt );
		}
		else
		{
			value = m_mouth / 64.0f;
			if (value > 1.0f) value = 1.0f;
			value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			// print_log("{} {}\n", mouthopen, value );
		}
		switch (pbonecontroller[j].type & STUDIO_TYPES)
		{
		case STUDIO_XR:
		case STUDIO_YR:
		case STUDIO_ZR:
			m_adj[j] = value * (HL_PI / 180.0f);
			break;
		case STUDIO_X:
		case STUDIO_Y:
		case STUDIO_Z:
			m_adj[j] = value;
			break;
		}
	}
}

void StudioModel::CalcBoneQuaternion(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, vec4& q)
{
	// Valve
	int					j, k;
	vec4				q1, q2;
	vec3				angle1, angle2;
	mstudioanimvalue_t* panimvalue;

	for (j = 0; j < 3; j++)
	{
		if (panim->offset[j + 3] == 0)
		{
			angle2[j] = angle1[j] = pbone->value[j + 3]; // default;
		}
		else
		{
			panimvalue = (mstudioanimvalue_t*)((unsigned char*)panim + panim->offset[j + 3]);
			k = frame;
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// Bah, missing blend!
			if (panimvalue->num.valid > k)
			{
				angle1[j] = panimvalue[k + 1].value;

				if (panimvalue->num.valid > k + 1)
				{
					angle2[j] = panimvalue[k + 2].value;
				}
				else
				{
					if (panimvalue->num.total > k + 1)
						angle2[j] = angle1[j];
					else
						angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			else
			{
				angle1[j] = panimvalue[panimvalue->num.valid].value;
				if (panimvalue->num.total > k + 1)
				{
					angle2[j] = angle1[j];
				}
				else
				{
					angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			angle1[j] = pbone->value[j + 3] + angle1[j] * pbone->scale[j + 3];
			angle2[j] = pbone->value[j + 3] + angle2[j] * pbone->scale[j + 3];
		}

		if (pbone->bonecontroller[j + 3] != -1)
		{
			angle1[j] += m_adj[pbone->bonecontroller[j + 3]];
			angle2[j] += m_adj[pbone->bonecontroller[j + 3]];
		}
	}

	if (!VectorCompare(angle1, angle2))
	{
		AngleQuaternion(angle1, q1);
		AngleQuaternion(angle2, q2);
		QuaternionSlerp(q1, q2, s, q);
	}
	else
	{
		AngleQuaternion(angle1, q);
	}
}


void StudioModel::CalcBonePosition(int frame, float s, mstudiobone_t* pbone, mstudioanim_t* panim, vec3& pos)
{
	// Valve
	int					j, k;
	mstudioanimvalue_t* panimvalue;

	for (j = 0; j < 3; j++)
	{
		pos[j] = pbone->value[j]; // default;
		if (panim->offset[j] != 0)
		{
			panimvalue = (mstudioanimvalue_t*)((unsigned char*)panim + panim->offset[j]);

			k = frame;
			// find span of values that includes the frame we want
			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;
			}
			// if we're inside the span
			if (panimvalue->num.valid > k)
			{
				// and there's more data in the span
				if (panimvalue->num.valid > k + 1)
				{
					pos[j] += (panimvalue[k + 1].value * (1.0f - s) + s * panimvalue[k + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[k + 1].value * pbone->scale[j];
				}
			}
			else
			{
				// are we at the end of the repeating values section and there's another section with data?
				if (panimvalue->num.total <= k + 1)
				{
					pos[j] += (panimvalue[panimvalue->num.valid].value * (1.0f - s) + s * panimvalue[panimvalue->num.valid + 2].value) * pbone->scale[j];
				}
				else
				{
					pos[j] += panimvalue[panimvalue->num.valid].value * pbone->scale[j];
				}
			}
		}
		if (pbone->bonecontroller[j] != -1)
		{
			pos[j] += m_adj[pbone->bonecontroller[j]];
		}
	}
}


void StudioModel::CalcRotations(vec3* pos, vec4* q, mstudioseqdesc_t* pseqdesc, mstudioanim_t* panim, float f)
{
	// Valve
	int		i, frame;
	float		s;
	mstudiobone_t* pbone;

	// bah, fix this bug with changing sequences too fast
	if (f > pseqdesc->numframes - 1)
	{
		f = 0.0f;
	}
	else if (f < -0.01f)
	{
		f = -0.01f;
	}

	frame = (int)f;
	s = (f - frame);

	// add in programatic controllers
	CalcBoneAdj();

	pbone = (mstudiobone_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->boneindex);
	for (i = 0; i < m_pstudiohdr->numbones; i++, pbone++, panim++)
	{
		CalcBoneQuaternion(frame, s, pbone, panim, q[i]);
		CalcBonePosition(frame, s, pbone, panim, pos[i]);
	}

	if (pseqdesc->motiontype & STUDIO_X)
		pos[pseqdesc->motionbone][0] = 0.0f;
	if (pseqdesc->motiontype & STUDIO_Y)
		pos[pseqdesc->motionbone][1] = 0.0f;
	if (pseqdesc->motiontype & STUDIO_Z)
		pos[pseqdesc->motionbone][2] = 0.0f;
}


mstudioanim_t* StudioModel::GetAnim(mstudioseqdesc_t* pseqdesc)
{
	// Valve
	mstudioseqgroup_t* pseqgroup;
	pseqgroup = (mstudioseqgroup_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqgroupindex) + pseqdesc->seqgroup;

	if (pseqdesc->seqgroup == 0)
	{
		return (mstudioanim_t*)((unsigned char*)m_pstudiohdr + pseqgroup->unused2 /* was pseqgroup->data, will be almost always be 0 */ + pseqdesc->animindex);
	}

	return (mstudioanim_t*)((unsigned char*)m_panimhdr[pseqdesc->seqgroup] + pseqdesc->animindex);
}


void StudioModel::SlerpBones(vec4 q1[], vec3 pos1[], vec4 q2[], vec3 pos2[], float s)
{
	// Valve
	int			i;
	vec4		q3;
	float		s1;

	if (s < 0.0f) s = +0.0f;
	else if (s > 1.0) s = 1.0f;

	s1 = 1.0f - s;

	for (i = 0; i < m_pstudiohdr->numbones; i++)
	{
		QuaternionSlerp(q1[i], q2[i], s, q3);
		q1[i][0] = q3[0];
		q1[i][1] = q3[1];
		q1[i][2] = q3[2];
		q1[i][3] = q3[3];
		pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s;
		pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s;
		pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s;
	}
}


void StudioModel::AdvanceFrame(float dt)
{
	if (!m_pstudiohdr)
		return;
	mstudioseqdesc_t* pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + m_sequence;


	m_frame += dt * pseqdesc->fps;

	if (pseqdesc->numframes <= 1)
	{
		m_frame = 0.0f;
	}
	else
	{
		m_frame += (int)(m_frame / (pseqdesc->numframes - 1)) * (pseqdesc->numframes - 1);
	}

	if (m_frame >= pseqdesc->numframes)
		m_frame = 0;
}

void StudioModel::SetUpBones(void)
{
	// valve
	int					i;

	mstudiobone_t* pbones;
	mstudioseqdesc_t* pseqdesc;
	mstudioanim_t* panim;


	if (m_sequence >= m_pstudiohdr->numseq) {
		m_sequence = 0;
	}

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + m_sequence;

	panim = GetAnim(pseqdesc);
	CalcRotations(static_pos1, static_q1, pseqdesc, panim, m_frame);

	if (pseqdesc->numblends > 1)
	{
		float				s;

		panim += m_pstudiohdr->numbones;
		CalcRotations(static_pos2, static_q2, pseqdesc, panim, m_frame);
		s = m_blending[0] / 255.0f;

		SlerpBones(static_q1, static_pos1, static_q2, static_pos2, s);

		if (pseqdesc->numblends == 4)
		{
			panim += m_pstudiohdr->numbones;
			CalcRotations(static_pos3, static_q3, pseqdesc, panim, m_frame);

			panim += m_pstudiohdr->numbones;
			CalcRotations(static_pos4, static_q4, pseqdesc, panim, m_frame);

			s = m_blending[0] / 255.0f;
			SlerpBones(static_q3, static_pos3, static_q4, static_pos4, s);

			s = m_blending[1] / 255.0f;
			SlerpBones(static_q1, static_pos1, static_q3, static_pos3, s);
		}
	}

	pbones = (mstudiobone_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->boneindex);

	for (i = 0; i < m_pstudiohdr->numbones; i++) {
		QuaternionMatrix(static_q1[i], static_bonematrix);

		static_bonematrix[0][3] = static_pos1[i][0];
		static_bonematrix[1][3] = static_pos1[i][1];
		static_bonematrix[2][3] = static_pos1[i][2];

		if (pbones[i].parent == -1) {
			memcpy(g_bonetransform[i], static_bonematrix, sizeof(float) * 12);
		}
		else {
			R_ConcatTransforms(g_bonetransform[pbones[i].parent], static_bonematrix, g_bonetransform[i]);
		}
	}
}



/*
Not used
*/
void StudioModel::Lighting(float* lv, int bone, int flags, const vec3& normal)
{
	float 	illum;
	float	lightcos;

	illum = g_ambientlight * 1.0f;

	if (flags & STUDIO_NF_FLATSHADE)
	{
		illum += g_shadelight * 0.8f;
	}
	else
	{
		float r;
		lightcos = dotProduct(normal, g_blightvec[bone]); // -1 colinear, 1 opposite

		if (lightcos > 1.0f)
			lightcos = 1.0f;

		illum += g_shadelight;

		r = g_lambert;
		if (r <= 1.0f)
			r = 1.0f;

		lightcos = (lightcos + (r - 1.0f)) / r; 		// do modified hemispherical lighting
		if (lightcos > 0.0f)
		{
			illum -= g_shadelight * lightcos;
		}
		if (illum <= 0.0f)
			illum = 0.0f;
	}

	if (illum > 255.0f)
		illum = 255.0f;
	*lv = illum / 255.0f;	// Light from 0 to 1.0
}

/*
Not used
*/
void StudioModel::Chrome(int* pchrome, int bone, const vec3& normal)
{
	float n;

	if (g_chromeage[bone] != g_smodels_total)
	{
		// calculate vectors from the viewer to the bone. This roughly adjusts for position
		vec3 chromeupvec = vec3();		// g_chrome t vector in world reference frame
		vec3 chromerightvec = vec3();	// g_chrome s vector in world reference frame
		vec3 tmp = vec3();				// vector pointing at bone in world reference frame
		tmp[0] = g_bonetransform[bone][0][3];
		tmp[1] = g_bonetransform[bone][1][3];
		tmp[2] = g_bonetransform[bone][2][3];
		VectorNormalize(tmp);
		mCrossProduct(tmp, g_vright, chromeupvec);
		VectorNormalize(chromeupvec);
		mCrossProduct(tmp, chromeupvec, chromerightvec);
		VectorNormalize(chromerightvec);

		VectorIRotate(chromeupvec, g_bonetransform[bone], g_chromeup[bone]);
		VectorIRotate(chromerightvec, g_bonetransform[bone], g_chromeright[bone]);

		g_chromeage[bone] = g_smodels_total;
	}

	// calc s coord
	n = dotProduct(normal, g_chromeright[bone]);
	pchrome[0] = (int)round((n + 1.0f) * 32.0f);

	// calc t coord
	n = dotProduct(normal, g_chromeup[bone]);
	pchrome[1] = (int)round((n + 1.0f) * 32.0f);
}


/*
 not used
*/
void StudioModel::SetupLighting()
{
	int i;
	// TODO: only do it for bones that actually have textures
	for (i = 0; i < m_pstudiohdr->numbones; i++)
	{
		VectorIRotate(g_lightvec, g_bonetransform[i], g_blightvec[i]);
	}
}

void StudioModel::SetupModel(int bodypart)
{
	int index;

	if (bodypart >= m_pstudiohdr->numbodyparts || bodypart < 0)
	{
		print_log(get_localized_string(LANG_0979), bodypart);
		bodypart = 0;
	}

	if (m_pstudiohdr->bodypartindex < 0)
	{
		print_log(get_localized_string(LANG_0980), m_pstudiohdr->bodypartindex);
	}

	mstudiobodyparts_t* pbodypart = (mstudiobodyparts_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bodypartindex) + bodypart;
	if (pbodypart->nummodels <= 0)
	{
		m_pmodel = (mstudiomodel_t*)((unsigned char*)m_pstudiohdr + pbodypart->modelindex);
	}
	else
	{
		index = m_bodynum / pbodypart->base;
		index = index % pbodypart->nummodels;
		m_pmodel = (mstudiomodel_t*)((unsigned char*)m_pstudiohdr + pbodypart->modelindex) + index;
	}


	if (m_ptexturehdr && m_ptexturehdr->skinindex < 0)
	{
		print_log(get_localized_string(LANG_0981), m_ptexturehdr->skinindex);
	}

	if (m_pmodel->normindex < 0)
	{
		print_log(get_localized_string(LANG_0982), m_pmodel->normindex);
	}

	if (m_pmodel->vertindex < 0)
	{
		print_log(get_localized_string(LANG_0983), m_pmodel->vertindex);
	}

	if (m_pmodel->vertinfoindex < 0)
	{
		print_log(get_localized_string(LANG_0984), m_pmodel->vertinfoindex);
	}

	if (m_ptexturehdr && m_ptexturehdr->textureindex < 0)
	{
		print_log(get_localized_string(LANG_0985), m_ptexturehdr->textureindex);
	}
}


void StudioModel::UpdateModelMeshList()
{
	if (!m_pstudiohdr || m_pstudiohdr->numbodyparts == 0)
		return;

	int i;

	g_smodels_total++; // render data cache cookie

	SetUpBones();

	//SetupLighting();

	if ((int)mdl_mesh_groups.size() < m_pstudiohdr->numbodyparts)
		mdl_mesh_groups.resize(m_pstudiohdr->numbodyparts);

	for (i = 0; i < m_pstudiohdr->numbodyparts; i++)
	{
		SetupModel(i);
		RefreshMeshList(i);
	}
}

void StudioModel::RefreshMeshList(int body)
{
	if (!m_pstudiohdr)
		return;
	StudioMesh tmpStudioMesh = StudioMesh();
	mstudiomesh_t* pmesh;
	unsigned char* pvertbone;
	unsigned char* pnormbone;
	vec3* pstudioverts;
	vec3* pstudionorms;
	mstudiotexture_t* ptexture;
	float				lv_tmp;
	short* pskinref;

	if (needForceUpdate)
	{
		mins = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
		maxs = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	}

	pvertbone = ((unsigned char*)m_pstudiohdr + m_pmodel->vertinfoindex);
	pnormbone = ((unsigned char*)m_pstudiohdr + m_pmodel->norminfoindex);
	ptexture = m_ptexturehdr ? (mstudiotexture_t*)((unsigned char*)m_ptexturehdr + m_ptexturehdr->textureindex) : NULL;

	pmesh = (mstudiomesh_t*)((unsigned char*)m_pstudiohdr + m_pmodel->meshindex);

	pstudioverts = (vec3*)((unsigned char*)m_pstudiohdr + m_pmodel->vertindex);
	pstudionorms = (vec3*)((unsigned char*)m_pstudiohdr + m_pmodel->normindex);

	pskinref = m_ptexturehdr ? (short*)((unsigned char*)m_ptexturehdr + m_ptexturehdr->skinindex) : NULL;

	if (m_ptexturehdr && m_ptexturehdr->skinindex < 0)
	{
		print_log(get_localized_string(LANG_1137), m_ptexturehdr->skinindex);
	}

	if (m_pmodel->normindex < 0)
	{
		print_log(get_localized_string(LANG_1138), m_pmodel->normindex);
	}

	if (m_pmodel->vertindex < 0)
	{
		print_log(get_localized_string(LANG_1139), m_pmodel->vertindex);
	}

	if (m_pmodel->vertinfoindex < 0)
	{
		print_log(get_localized_string(LANG_1140), m_pmodel->vertinfoindex);
	}

	if (m_ptexturehdr && m_ptexturehdr->textureindex < 0)
	{
		print_log(get_localized_string(LANG_1141), m_ptexturehdr->textureindex);
	}

	if (pskinref && m_ptexturehdr && m_skinnum >= 0 && m_skinnum < m_ptexturehdr->numskinfamilies)
		pskinref += (m_skinnum * m_ptexturehdr->numskinref);

	for (int i = 0; i < m_pmodel->numverts; i++)
	{
		VectorTransform(pstudioverts[i], g_bonetransform[pvertbone[i]], g_xformverts[i]);
	}

	//
	// clip and draw all triangles
	//

	for (int j = 0; j < m_pmodel->nummesh; j++)
	{
		int flags = 0;
		if (ptexture && pskinref)
		{
			flags = ptexture[pskinref[pmesh[j].skinref]].flags;
		}
		for (int i = 0; i < pmesh[j].numnorms; i++, pstudionorms++, pnormbone++)
		{
			Lighting(&lv_tmp, *pnormbone, flags, *pstudionorms);

			// FIX: move this check out of the inner loop
			if (flags & STUDIO_NF_CHROME)
				Chrome(g_chrome[i], *pnormbone, *pstudionorms);

			g_lightvalues[i][0] = g_lightcolor[0] * lv_tmp;
			g_lightvalues[i][1] = g_lightcolor[1] * lv_tmp;
			g_lightvalues[i][2] = g_lightcolor[2] * lv_tmp;
		}
	}

	if ((int)mdl_mesh_groups[body].size() < m_pmodel->nummesh)
	{
		mdl_mesh_groups[body].resize(m_pmodel->nummesh);

		for (int j = 0; j < m_pmodel->nummesh; j++)
		{
			mdl_mesh_groups[body][j].buffer = new VertexBuffer(g_app->modelShader, NULL, 0, GL_TRIANGLES);
		}
	}

	for (int j = 0; j < m_pmodel->nummesh; j++)
	{
		short* ptricmds;

		pmesh = (mstudiomesh_t*)((unsigned char*)m_pstudiohdr + m_pmodel->meshindex) + j;
		ptricmds = (short*)((unsigned char*)m_pstudiohdr + pmesh->triindex);
		int texidx = ptexture && pskinref ? ptexture[pskinref[pmesh->skinref]].index : 0;
		if (mdl_textures.size())
		{
			if (texidx < (int)mdl_textures.size())
			{
				mdl_mesh_groups[body][j].texture = mdl_textures[texidx];
			}
			else
			{
				mdl_mesh_groups[body][j].texture = NULL;
			}
		}
		else
		{
			mdl_mesh_groups[body][j].texture = NULL;
		}


		int totalElements = 0;
		int texCoordIdx = 0;
		//int colorIdx = 0;
		int vertexIdx = 0;
		while (int i = *(ptricmds++))
		{
			int drawMode = GL_TRIANGLE_STRIP;
			if (i < 0)
			{
				i = -i;
				drawMode = GL_TRIANGLE_FAN;
			}

			int polies = i - 2;
			int elementsThisStrip = 0;
			int fanStartVertIdx = vertexIdx;
			int fanStartTexIdx = texCoordIdx;
			//int fanStartColorIdx = colorIdx;

			for (; i > 0; i--, ptricmds += 4)
			{

				if (elementsThisStrip++ >= 3) {
					int v1PosIdx = fanStartVertIdx;
					int v2PosIdx = vertexIdx - 3 * 1;
					int v1TexIdx = fanStartTexIdx;
					int v2TexIdx = texCoordIdx - 2 * 1;
					//int v1ColorIdx = fanStartColorIdx;
					//int v2ColorIdx = colorIdx - 4 * 1;

					if (drawMode == GL_TRIANGLE_STRIP) {
						v1PosIdx = vertexIdx - 3 * 2;
						v2PosIdx = vertexIdx - 3 * 1;
						v1TexIdx = texCoordIdx - 2 * 2;
						v2TexIdx = texCoordIdx - 2 * 1;
						//v1ColorIdx = colorIdx - 4 * 2;
						//v2ColorIdx = colorIdx - 4 * 1;
					}

					texCoordData[texCoordIdx++] = texCoordData[v1TexIdx];
					texCoordData[texCoordIdx++] = texCoordData[v1TexIdx + 1];
					/*colorData[colorIdx++] = colorData[v1ColorIdx];
					colorData[colorIdx++] = colorData[v1ColorIdx + 1];
					colorData[colorIdx++] = colorData[v1ColorIdx + 2];
					colorData[colorIdx++] = colorData[v1ColorIdx + 3];*/
					vertexData[vertexIdx++] = vertexData[v1PosIdx];
					vertexData[vertexIdx++] = vertexData[v1PosIdx + 1];
					vertexData[vertexIdx++] = vertexData[v1PosIdx + 2];

					texCoordData[texCoordIdx++] = texCoordData[v2TexIdx];
					texCoordData[texCoordIdx++] = texCoordData[v2TexIdx + 1];
					/*colorData[colorIdx++] = colorData[v2ColorIdx];
					colorData[colorIdx++] = colorData[v2ColorIdx + 1];
					colorData[colorIdx++] = colorData[v2ColorIdx + 2];
					colorData[colorIdx++] = colorData[v2ColorIdx + 3];*/
					vertexData[vertexIdx++] = vertexData[v2PosIdx];
					vertexData[vertexIdx++] = vertexData[v2PosIdx + 1];
					vertexData[vertexIdx++] = vertexData[v2PosIdx + 2];

					totalElements += 2;
					elementsThisStrip += 2;
				}
				float s = 1.0;
				float t = 1.0;
				if (ptexture && pskinref)
				{
					s /= (float)ptexture[pskinref[pmesh->skinref]].width;
					t /= (float)ptexture[pskinref[pmesh->skinref]].height;
				}
				// FIX: put these in as integer coords, not floats
				if (ptexture && pskinref && ptexture[pskinref[pmesh->skinref]].flags & STUDIO_NF_CHROME)
				{
					texCoordData[texCoordIdx++] = g_chrome[ptricmds[1]][0] * s;
					texCoordData[texCoordIdx++] = g_chrome[ptricmds[1]][1] * t;
				}
				else if (ptexture && pskinref && ptexture[pskinref[pmesh->skinref]].flags & STUDIO_NF_UV_COORDS)
				{
					texCoordData[texCoordIdx++] = half_prefloat(*(unsigned short*)&ptricmds[2]);
					texCoordData[texCoordIdx++] = half_prefloat(*(unsigned short*)&ptricmds[3]);
				}
				else
				{
					texCoordData[texCoordIdx++] = ptricmds[2] * s;
					texCoordData[texCoordIdx++] = ptricmds[3] * t;
				}

				/*lv = &g_pvlightvalues[ptricmds[1]];
				colorData[colorIdx++] = lv->x;
				colorData[colorIdx++] = lv->y;
				colorData[colorIdx++] = lv->z;
				colorData[colorIdx++] = 1.0;*/

				vec3* av = &g_xformverts[ptricmds[0]];
				vertexData[vertexIdx++] = av->x;
				vertexData[vertexIdx++] = av->y;
				vertexData[vertexIdx++] = av->z;


				totalElements++;
			}
			if (drawMode == GL_TRIANGLE_STRIP) {
				for (int p = 1; p < polies; p += 2) {
					int polyOffset = p * 3;

					for (int k = 0; k < 3; k++)
					{
						int vstart = polyOffset * 3 + fanStartVertIdx + k;
						float t = vertexData[vstart];
						vertexData[vstart] = vertexData[vstart + 3];
						vertexData[vstart + 3] = t;
					}
					for (int k = 0; k < 2; k++)
					{
						int vstart = polyOffset * 2 + fanStartTexIdx + k;
						float t = texCoordData[vstart];
						texCoordData[vstart] = texCoordData[vstart + 2];
						texCoordData[vstart + 2] = t;
					}
					/*for (int k = 0; k < 4; k++)
					{
						int vstart = polyOffset * 4 + fanStartColorIdx + k;
						float t = colorData[vstart];
						colorData[vstart] = colorData[vstart + 4];
						colorData[vstart + 4] = t;
					}*/
				}
			}
		}

		if ((int)mdl_mesh_groups[body][j].verts.size() < totalElements)
		{
			mdl_mesh_groups[body][j].verts.resize(totalElements);
			mdl_mesh_groups[body][j].buffer->setData(&mdl_mesh_groups[body][j].verts[0], (int)(mdl_mesh_groups[body][j].verts.size()));
		}
		for (int z = 0; z < (int)mdl_mesh_groups[body][j].verts.size(); z++)
		{
			mdl_mesh_groups[body][j].verts[z].u = texCoordData[z * 2 + 0];
			mdl_mesh_groups[body][j].verts[z].v = texCoordData[z * 2 + 1];
			/*mdl_mesh_groups[body][j].verts[z].r = colorData[z * 4 + 0];
			mdl_mesh_groups[body][j].verts[z].g = colorData[z * 4 + 1];
			mdl_mesh_groups[body][j].verts[z].b = colorData[z * 4 + 2];
			mdl_mesh_groups[body][j].verts[z].a = 1.0;*/
			mdl_mesh_groups[body][j].verts[z].pos.x = vertexData[z * 3 + 0];
			mdl_mesh_groups[body][j].verts[z].pos.y = vertexData[z * 3 + 2];
			mdl_mesh_groups[body][j].verts[z].pos.z = -vertexData[z * 3 + 1];

			if (needForceUpdate)
			{
				expandBoundingBox(vec3(vertexData[z * 3 + 0], vertexData[z * 3 + 1], vertexData[z * 3 + 2]),
					mins, maxs);
			}
		}
	}

	if (needForceUpdate)
	{
		if (std::abs(mins.x - maxs.x) > 512.f &&
			std::abs(mins.y - maxs.y) > 512.f &&
			std::abs(mins.z - maxs.z) > 512.f)
			ExtractBBox(mins, maxs);

		if (mdl_cube != NULL)
		{
			mdl_cube->mins = mins;
			mdl_cube->maxs = maxs;
			g_app->pointEntRenderer->genCubeBuffers(mdl_cube);
		}
		else
		{
			mdl_cube = new EntCube();
			mdl_cube->color = { 255, 255, 0, 255 };
			mdl_cube->mins = mins;
			mdl_cube->maxs = maxs;
			g_app->pointEntRenderer->genCubeBuffers(mdl_cube);
		}
	}
}


void StudioModel::UploadTexture(mstudiotexture_t* ptexture, unsigned char* data, COLOR3* pal)
{
	int texsize = ptexture->width * ptexture->height;

	COLOR4* out = new COLOR4[texsize];

	if (ptexture->flags & 0x64)
	{
		for (int i = 0; i < texsize; i++)
		{
			if (data[i] == 255)
				out[i].a = out[i].b = out[i].g = out[i].r = 0;
			else
				out[i] = pal[data[i]];
		}
	}
	else
	{
		for (int i = 0; i < texsize; i++)
		{
			out[i] = pal[data[i]];
		}
	}
	//print_log("Texture name {} texture flags {}\n", ptexture->name, ptexture->flags);
	// ptexture->width = outwidth;
	// ptexture->height = outheight;

	auto texture = new Texture(ptexture->width, ptexture->height, (unsigned char*)out, ptexture->name[0] != '\0' ? stripExt(ptexture->name) : "UNNAMED", true);
	texture->setWadName("model_textures");
	texture->upload();
	ptexture->index = (int)mdl_textures.size();
	mdl_textures.push_back(texture);
}




studiohdr_t* StudioModel::LoadModel(const std::string & modelname, bool IsTexture)
{
	int size;
	char* buffer = loadFile(modelname, size);
	if (!buffer)
	{
		print_log(get_localized_string(LANG_0986), modelname);
		return NULL;
	}

	unsigned char* pin = (unsigned char*)buffer;
	studiohdr_t* phdr = (studiohdr_t*)buffer;

	if (phdr->id != 'TSDI' || (phdr->name[0] == '\0' && !IsTexture))
	{
		delete buffer;
		return NULL;
	}

	if (phdr->textureindex != 0)
	{
		mstudiotexture_t* ptexture = (mstudiotexture_t*)(pin + phdr->textureindex);
		for (int i = 0; i < phdr->numtextures; i++)
		{
			// strncpy( name, mod->name );
			// strncpy( name, ptexture[i].name );
			UploadTexture(&ptexture[i], pin + ptexture[i].index, (COLOR3*)(pin + (ptexture[i].width * ptexture[i].height + ptexture[i].index)));
		}
	}

	return (studiohdr_t*)buffer;
}


studioseqhdr_t* StudioModel::LoadDemandSequences(const std::string& modelname, int seqid)
{
	std::ostringstream str;
	str << modelname.substr(0, modelname.size() - 4) << std::setw(2) << std::setfill('0') << seqid << ".mdl";

	int size;
	void* buffer = loadFile(str.str(), size);
	if (!buffer)
	{
		print_log(get_localized_string(LANG_0987), str.str());
		return NULL;
	}
	return (studioseqhdr_t*)buffer;
}

void StudioModel::GetModelMeshes(int& bodies, int& subbodies, int& skins, int& meshes)
{

}

void StudioModel::DrawMDL(int meshnum)
{
	if (frametime < 0.0f)
		frametime = g_app->curTime;

	if (needForceUpdate || (g_app->curTime - frametime > (1.0f / fps) && (g_render_flags & RENDER_MODELS_ANIMATED)))
	{
		if (needForceUpdate)
		{
			if (mdl_mesh_groups.size())
			{
				for (auto& body : mdl_mesh_groups)
				{
					if (body.size())
					{
						for (auto& submesh : body)
						{
							if (submesh.buffer)
							{
								delete submesh.buffer;
							}
						}
					}
				}
			}
			mdl_mesh_groups = std::vector<std::vector<StudioMesh>>();
		}

		if (mdl_mesh_groups.size())
		{
			for (auto& body : mdl_mesh_groups)
			{
				if (body.size())
				{
					for (auto& submesh : body)
					{
						if (submesh.buffer)
						{
							submesh.buffer->uploaded = false;
						}
					}
				}
			}
		}

		AdvanceFrame((1.0f / fps));
		UpdateModelMeshList();
		this->frametime = -1.0f;
	}

	needForceUpdate = false;


	if (meshnum >= 0)
	{
		if (mdl_mesh_groups.size() && meshnum < (int)mdl_mesh_groups[0].size())
		{
			Texture* validTexture = mdl_mesh_groups[0][meshnum].texture;

			if (mdl_mesh_groups[0][meshnum].texture)
			{
				mdl_mesh_groups[0][meshnum].texture->bind(0);
			}
			else if (validTexture)
			{
				validTexture->bind(0);
			}
			else
			{
				whiteTex->bind(0);
			}
			mdl_mesh_groups[0][meshnum].buffer->drawFull();
		}
	}
	else
	{
		for (size_t group = 0; group < mdl_mesh_groups.size(); group++)
		{
			for (size_t meshid = 0; meshid < mdl_mesh_groups[group].size(); meshid++)
			{
				Texture* validTexture = mdl_mesh_groups[group][meshid].texture;

				if (mdl_mesh_groups[group][meshid].texture)
				{
					mdl_mesh_groups[group][meshid].texture->bind(0);
				}
				else if (validTexture)
				{
					validTexture->bind(0);
				}
				else
				{
					whiteTex->bind(0);
				}
				mdl_mesh_groups[group][meshid].buffer->drawFull();
			}
		}
	}
}

void StudioModel::Init(const std::string& modelname)
{
	m_pstudiohdr = LoadModel(modelname);
	if (!m_pstudiohdr)
	{
		print_log(get_localized_string(LANG_0988), modelname);
		return;
	}
	if (g_settings.verboseLogs)
	{
		print_log(get_localized_string(LANG_0989), modelname, m_pstudiohdr->version);
	}
	// preload textures
	if (m_pstudiohdr->numtextures == 0)
	{
		m_ptexturehdr = LoadModel(modelname.substr(0, modelname.size() - 4) + "T.mdl", true);
	}
	else
	{
		m_ptexturehdr = m_pstudiohdr;
	}

	// preload animations
	if (m_pstudiohdr->numseqgroups > 1)
	{
		for (int i = 1; i < m_pstudiohdr->numseqgroups; i++)
		{
			m_panimhdr[i] = LoadDemandSequences(modelname, i);
		}
	}
}


////////////////////////////////////////////////////////////////////////

int StudioModel::GetSequence()
{
	return m_sequence;
}

int StudioModel::SetSequence(int iSequence)
{
	if (m_pstudiohdr)
	{
		if (iSequence > m_pstudiohdr->numseq)
			iSequence = 0;
		if (iSequence < 0)
			iSequence = m_pstudiohdr->numseq - 1;
	}
	if (iSequence != m_sequence)
	{
		needForceUpdate = true;
	}
	m_sequence = iSequence;
	m_frame = 0;

	return m_sequence;
}


void StudioModel::ExtractBBox(vec3& _mins, vec3& _maxs)
{
	mstudioseqdesc_t* pseqdesc;

	if (!m_pstudiohdr || m_sequence > m_pstudiohdr->numseq)
		return;
	if (m_sequence < 0)
		return;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex);
	
	_mins[0] = pseqdesc[m_sequence].bbmin[0];
	_mins[1] = pseqdesc[m_sequence].bbmin[1];
	_mins[2] = pseqdesc[m_sequence].bbmin[2];

	_maxs[0] = pseqdesc[m_sequence].bbmax[0];
	_maxs[1] = pseqdesc[m_sequence].bbmax[1];
	_maxs[2] = pseqdesc[m_sequence].bbmax[2];
}



void StudioModel::GetSequenceInfo(float* pflFrameRate, float* pflGroundSpeed)
{
	mstudioseqdesc_t* pseqdesc;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + (int)m_sequence;

	if (pseqdesc->numframes > 1)
	{
		*pflFrameRate = 256.0f * pseqdesc->fps / (pseqdesc->numframes - 1);
		*pflGroundSpeed = sqrt(pseqdesc->linearmovement[0] * pseqdesc->linearmovement[0] + pseqdesc->linearmovement[1] * pseqdesc->linearmovement[1] + pseqdesc->linearmovement[2] * pseqdesc->linearmovement[2]);
		*pflGroundSpeed = *pflGroundSpeed * pseqdesc->fps / (pseqdesc->numframes - 1);
	}
	else
	{
		*pflFrameRate = 256.0f;
		*pflGroundSpeed = 0.0f;
	}
}


float StudioModel::SetController(int iController, float flValue)
{
	if (!m_pstudiohdr)
		return 0.0f;
	int i = 0;
	mstudiobonecontroller_t* pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	// find first controller that matches the index
	for (i = 0; i < m_pstudiohdr->numbonecontrollers; i++, pbonecontroller++)
	{
		if (pbonecontroller->index == iController)
			break;
	}
	if (i >= m_pstudiohdr->numbonecontrollers)
		return flValue;

	// wrap 0..360 if it's a rotational controller
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0f >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0f) + 180.0f)
				flValue = flValue - 360.0f;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0f) - 180.0f)
				flValue = flValue + 360.0f;
		}
		else
		{
			if (flValue > 360.0f)
				flValue = flValue - (int)(flValue / 360.0f) * 360.0f;
			else if (flValue < 0.0f)
				flValue = flValue + (int)((flValue / -360.0f) + 1.0f) * 360.0f;
		}
	}

	float setting = 255.0f * (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);

	if (setting < 0.0f) setting = 0.0f;
	if (setting > 255.0f) setting = 255.0f;
	
	m_controller[iController] = (unsigned char)round(setting);

	return setting * (1.0f / 255.0f) * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}


float StudioModel::SetMouth(float flValue)
{
	if (!m_pstudiohdr)
		return 0.0f;
	mstudiobonecontroller_t* pbonecontroller = (mstudiobonecontroller_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bonecontrollerindex);

	// find first controller that matches the mouth
	for (int i = 0; i < m_pstudiohdr->numbonecontrollers; i++, pbonecontroller++)
	{
		if (pbonecontroller->index == 4)
			break;
	}

	// wrap 0..360 if it's a rotational controller
	if (pbonecontroller->type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pbonecontroller->end < pbonecontroller->start)
			flValue = -flValue;

		// does the controller not wrap?
		if (pbonecontroller->start + 359.0f >= pbonecontroller->end)
		{
			if (flValue > ((pbonecontroller->start + pbonecontroller->end) / 2.0f) + 180.0f)
				flValue = flValue - 360.0f;
			if (flValue < ((pbonecontroller->start + pbonecontroller->end) / 2.0f) - 180.0f)
				flValue = flValue + 360.0f;
		}
		else
		{
			if (flValue > 360.0f)
				flValue = flValue - (int)(flValue / 360.0f) * 360.0f;
			else if (flValue < 0.0f)
				flValue = flValue + (int)((flValue / -360.0f) + 1.0f) * 360.0f;
		}
	}

	float setting = 64.0f * (flValue - pbonecontroller->start) / (pbonecontroller->end - pbonecontroller->start);

	if (setting < 0.0f) setting = 0.0f;
	if (setting > 64.0f) setting = 64.0f;

	m_mouth = (unsigned char)round(setting);

	return setting * (1.0f / 64.0f) * (pbonecontroller->end - pbonecontroller->start) + pbonecontroller->start;
}


float StudioModel::SetBlending(int iBlender, float flValue)
{
	mstudioseqdesc_t* pseqdesc;

	pseqdesc = (mstudioseqdesc_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->seqindex) + (int)m_sequence;

	if (pseqdesc->blendtype[iBlender] == 0)
		return flValue;

	if (pseqdesc->blendtype[iBlender] & (STUDIO_XR | STUDIO_YR | STUDIO_ZR))
	{
		// ugly hack, invert value if end < start
		if (pseqdesc->blendend[iBlender] < pseqdesc->blendstart[iBlender])
			flValue = -flValue;

		// does the controller not wrap?
		if (pseqdesc->blendstart[iBlender] + 359.0f >= pseqdesc->blendend[iBlender])
		{
			if (flValue > ((pseqdesc->blendstart[iBlender] + pseqdesc->blendend[iBlender]) / 2.0f) + 180.0f)
				flValue = flValue - 360.0f;
			if (flValue < ((pseqdesc->blendstart[iBlender] + pseqdesc->blendend[iBlender]) / 2.0f) - 180.0f)
				flValue = flValue + 360.0f;
		}
	}

	float setting = 255.0f * (flValue - pseqdesc->blendstart[iBlender]) / (pseqdesc->blendend[iBlender] - pseqdesc->blendstart[iBlender]);

	if (setting < 0.0f) setting = 0.0f;
	if (setting > 255.0f) setting = 255.0f;

	m_blending[iBlender] = (unsigned char)round(setting);

	return setting * (1.0f / 255.0f) * (pseqdesc->blendend[iBlender] - pseqdesc->blendstart[iBlender]) + pseqdesc->blendstart[iBlender];
}



int StudioModel::SetBodygroup(int iGroup, int iValue)
{
	if (iGroup > m_pstudiohdr->numbodyparts || (iGroup == m_iGroup && iValue == m_iGroupValue))
		return -1;

	m_iGroup = iGroup;
	m_iGroupValue = iValue;

	mstudiobodyparts_t* pbodypart = (mstudiobodyparts_t*)((unsigned char*)m_pstudiohdr + m_pstudiohdr->bodypartindex) + iGroup;

	int iCurrent = (m_bodynum / pbodypart->base) % pbodypart->nummodels;

	if (iValue >= pbodypart->nummodels)
		return iCurrent;

	m_bodynum = (m_bodynum - (iCurrent * pbodypart->base) + (iValue * pbodypart->base));

	needForceUpdate = true;

	return iValue;
}


int StudioModel::SetSkin(int iValue)
{
	if (!m_pstudiohdr || iValue < m_pstudiohdr->numskinfamilies)
	{
		return m_skinnum;
	}

	if (iValue != m_skinnum)
		needForceUpdate = true;
	m_skinnum = iValue;

	return iValue;
}

std::map<unsigned int, StudioModel*> mdl_models;

StudioModel* AddNewModelToRender(const std::string& path, unsigned int sum)
{
	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), (unsigned int)path.size(), sum);

	if (mdl_models.find(crc32) != mdl_models.end())
	{
		return mdl_models[crc32];
	}
	else
	{
		StudioModel* newModel = new StudioModel(path); // memory leak (cache)
		mdl_models[crc32] = newModel;
		return newModel;
	}
}