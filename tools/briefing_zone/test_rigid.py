import math,unittest
from rigid import rigid_pair,transform,removal_edits

class RigidRevision(unittest.TestCase):
    def test_one_frame_preserves_all_pairwise_distances(self):
        f=rigid_pair([[1,2],[7,2]],[[10,20],[10,26]])
        points=[[1,2,3],[4,-6,0],[-2,13,4]]
        moved=[transform(p,f) for p in points]
        self.assertAlmostEqual(f['angle'],math.pi/2)
        for i,a in enumerate(points):
            for j,b in enumerate(points):self.assertAlmostEqual(math.dist(a[:2],b[:2]),math.dist(moved[i][:2],moved[j][:2]))
        self.assertEqual([p[2] for p in points],[p[2] for p in moved])

    def test_pair_does_not_scale_when_landmarks_have_roundoff(self):
        f=rigid_pair([[0,0],[5,0]],[[10,10],[15.00001,10]])
        self.assertAlmostEqual(math.dist(transform([0,0],f),transform([100,0],f)),100)
        self.assertAlmostEqual(f['landmarkLengthMismatch'],.00001)

    def test_remove_only_requested_whole_call_line(self):
        text='// before\r\n  CreateParticleSource (1,2,3,4,.3,"star")\r\n// keep\r\n'
        rows=[dict(index=7,kind='script',function='CreateParticleSource',sourceOffset=text.index('Create'),old=[2,3,4])]
        edits,removed=removal_edits(text,rows,dict(removeRows=[7]));e=edits[0]
        self.assertEqual(text[:e['start']]+e['new']+text[e['end']:],'// before\r\n// keep\r\n')
        self.assertEqual(removed[0]['originalLine'],2)

    def test_refuse_call_line_with_other_statement(self):
        text='  CreateParticleSource(1,2,3,4,.3,"x") KillScript()\n'
        with self.assertRaises(AssertionError):removal_edits(text,[dict(index=1,kind='script',function='CreateParticleSource',sourceOffset=2,old=[2,3,4])],dict(removeRows=[1]))

if __name__=='__main__':unittest.main()
