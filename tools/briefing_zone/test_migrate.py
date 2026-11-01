import unittest
from migrate import calls,number,decrypt_script,flatten,script_destination_z
from finish import encrypt
import base64,struct
import tempfile,contextlib,io
from pathlib import Path
from unittest.mock import patch
from migrate import save,load
from revise import adjust

class MigrationChecks(unittest.TestCase):
    def test_passability_correction_does_not_replace_setcp_offset(self):
        for command in ('SetCP','SetCPFast'):
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                row=dict(index=0,kind='script',function=command,identity='synthetic',inactive=False,new=[1,2,.05],navigationTarget=True)
                save(root/'rows.json',[row]);save(root/'plan-draft.json',dict(leaves=[]))
                save(root/'script-edits.json',[dict(row=0,new=v) for v in ('1','2','0.0500')])
                save(root/'compare-old.json',dict(points=[dict(index=0,inBounds=True,humanPassable=True)]))
                save(root/'compare-new.json',dict(points=[dict(index=0,inBounds=True,humanPassable=False,passableWithin2m=True,nearestPassable=[1.5,2,99],projectionDistance=.5)]))
                with patch('revise.compose'),patch('revise.requests'),contextlib.redirect_stdout(io.StringIO()):adjust(root,'unused')
                self.assertEqual(load(root/'rows.json')[0]['new'],[1.5,2,.05])
                self.assertEqual(load(root/'script-edits.json')[2]['new'],'0.0500')

    def test_setcp_preserves_relative_height_across_terrain_change(self):
        for command in ('SetCP','SetCPFast'):
            for offset in (0,1.5,-0.05):
                self.assertEqual(script_destination_z(command,offset,2.8,6.2),offset)

    def test_effect_keeps_world_height_above_old_surface(self):
        self.assertAlmostEqual(script_destination_z('CreateFX',3,2.8,6.2),6.4)
        self.assertAlmostEqual(script_destination_z('CreateParticleSource',0,2.8,6.2),3.4)

    def test_nested_arguments_and_quoted_commas(self):
        text='MoveToPoint(GetObjectByName("A, B"), -1.25, 2e1)'
        result=next((name,args) for name,args in calls(text) if name[0]=='MoveToPoint')
        self.assertEqual(len(result[1]),3)
        self.assertFalse(number(result[1][0]))
        self.assertTrue(number(result[1][1]))
        self.assertEqual(text[result[1][2][0][1]:result[1][2][0][2]],'2e1')

    def test_comments_and_string_prose_never_become_calls(self):
        text='// SetCP(x,1,2,3)\r\nConsoleString("MoveToPoint(x,4,5)") /* UMSentry(x,6,7) */\r\nRotateTo(x,8,9)'
        self.assertEqual([name[0] for name,args in calls(text)],['ConsoleString','RotateTo'])

    def test_symbolic_parameters_are_not_numeric_points(self):
        text='DistanceUnitPoint(this,x1,y1) #DontBeIdiot(GetObject(7),1,2,3,4)'
        args=next(args for name,args in calls(text) if name[0]=='DistanceUnitPoint')
        self.assertFalse(number(args[1]))
        custom=next(args for name,args in calls(text) if name[0]=='#DontBeIdiot')
        self.assertEqual(len(custom),5)

    def test_single_quoted_prose_and_id_argument(self):
        text="ConsoleString('GetObject(67)') GetObjectByID('159')"
        result=list(calls(text))
        self.assertEqual([name[0] for name,args in result],['ConsoleString','GetObjectByID'])
        self.assertEqual(result[1][1][0][0][0],"'159'")

    def test_script_cipher_preserves_all_bytes_and_original_key(self):
        plain=b'//\r\n'+bytes(range(256))+b'\0'
        encrypted=encrypt(plain,0x12345678)
        document={'sections':[{'type':0xacceeccb,'payload':base64.b64encode(encrypted).decode()}]}
        self.assertEqual(decrypt_script(document).encode('latin1'),plain)
        self.assertEqual(encrypted[:4],struct.pack('<I',0x12345678))

if __name__=='__main__':unittest.main()
