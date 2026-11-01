import unittest
from prune import merge_zone

class CatalogMerge(unittest.TestCase):
    baseline=b'#zone bz1g gipat brief\r\n#res\r\nbasegipat basecam\r\n#exit 1\r\ngz1g_bz1g 1\r\n#deploy\r\n1 2 3 4\r\n#remove\r\n5 6 7 8\r\n#restrict\r\n9 10 11\r\n#zone other x game\r\n#res\r\nold old\r\n'
    reviewed=baseline.replace(b'basegipat basecam',b'zone1 basecam').replace(b'1 2 3 4',b'11 12 13 14')

    def test_merge_preserves_new_live_changes_to_other_zone(self):
        live=self.baseline.replace(b'old old',b'owner owner')
        result=merge_zone(live,self.baseline,self.reviewed,'bz1g')
        self.assertIn(b'zone1 basecam',result);self.assertIn(b'owner owner',result)
        self.assertNotIn(b'old old',result);self.assertIn(b'gz1g_bz1g 1',result)

    def test_refuse_changed_live_target(self):
        with self.assertRaises(ValueError):merge_zone(self.baseline.replace(b'1 2 3 4',b'2 3 4 5'),self.baseline,self.reviewed,'bz1g')

    def test_refuse_semantic_exit_change(self):
        with self.assertRaises(ValueError):merge_zone(self.baseline,self.baseline,self.reviewed.replace(b'gz1g_bz1g 1',b'gz1g 2'),'bz1g')

    def test_refuse_duplicate_zone(self):
        with self.assertRaises(ValueError):merge_zone(self.baseline+self.baseline,self.baseline,self.reviewed,'bz1g')

if __name__=='__main__':unittest.main()
