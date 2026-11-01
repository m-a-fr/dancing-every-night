"""Regression checks for musical motif memory and per-difficulty travel time."""
import unittest
import random
from types import SimpleNamespace
import mapper

class ArrangerTests(unittest.TestCase):
    def test_new_gestures_have_distinct_touch_relations(self):
        def seq(feel):return mapper.feel_template(feel,6,'L',0,random.Random(1))
        pedal,relay,turn=map(seq,('pedal_return','relay_pair','turnaround'))
        self.assertEqual(pedal[0],pedal[2])
        self.assertNotEqual(pedal[0],pedal[1])
        self.assertEqual(relay[1],relay[2])
        self.assertNotEqual(relay[0][0],relay[1][0])
        self.assertEqual(turn[0],turn[4])
        self.assertEqual(turn[1][0],turn[2][0])
        self.assertEqual(len({tuple(pedal),tuple(relay),tuple(turn)}),3)

    def test_incomplete_gestures_are_not_selected(self):
        a=self.arranger()
        for n,forbidden in ((2,{'pedal_return','relay_pair','turnaround'}),(3,{'pedal_return','turnaround'}),(4,{'turnaround'})):
            for i in range(100):self.assertNotIn(a.choose_feel(0,4,n,False)[0],forbidden)

    def arranger(self, similarity=1.0):
        song=SimpleNamespace(bpm=120,meter=4,bar0=0,sim=lambda a,b,n:similarity,
                             slot=lambda b:{'P':60,'V':False,'K':0.,'S':0.,'E':1.})
        comp=mapper.Composer(song,[],0,32)
        return mapper.Arranger(song,comp.cfg('NORMAL'),'NORMAL',7,
                              dict(repeat=0.,motion=0.,dialog=0.,steady=False,slow=False))

    def test_repeat_memory_keeps_the_original_and_counts_uses(self):
        a=self.arranger();notes=[(b,'CROSS','TAP',None) for b in range(16)]
        a.arrange(notes,0,16,False)
        self.assertEqual(len(a.memory),1)
        self.assertEqual(a.memory[0][3],4)
        self.assertEqual(a.forms['variation'],1)
        self.assertEqual(a.forms['répétition'],2)

    def test_equal_counts_do_not_make_different_rhythms_answers(self):
        a=self.arranger(0.0)
        notes=[(0,'CROSS','TAP',None),(1,'UP','TAP',None),(2,'CIRCLE','TAP',None),
               (4,'CROSS','TAP',None),(5.5,'UP','TAP',None),(7,'CIRCLE','TAP',None)]
        a.arrange(notes,0,8,False)
        self.assertEqual(a.forms['réponse en miroir'],0)

    def test_silence_does_not_advance_the_phrase_as_a_played_bar(self):
        a=self.arranger(0.0)
        notes=[(0,'CROSS','TAP',None),(1,'UP','TAP',None),
               (12,'CIRCLE','TAP',None),(13,'UP','TAP',None)]
        a.arrange(notes,0,16,False)
        self.assertEqual(a.forms['réponse en miroir'],0)
        self.assertEqual(a.forms['cadence'],1)

    def test_travel_bounds_and_legacy_default(self):
        for bpm in (80,120,170,240):
            vals=[mapper.approach_ms_for(bpm) for d in mapper.DIFF]
            self.assertEqual(vals,[1500]*4)
            self.assertEqual(mapper.approach_ms_for(bpm),1500)
            for d,ms in zip(mapper.DIFF,vals):
                txt=mapper.chart_text(dict(id='test',title='test',audio='audio.ogg'),d,[],{},bpm,0)
                self.assertEqual(('APPROACH_MS '+str(ms)) in txt,ms!=1500)
                self.assertGreaterEqual(mapper.same_lane_beats(bpm,ms)*60000/bpm,ms*.18-1e-6)

    def test_author_choices_do_not_impose_a_difficulty_ladder(self):
        song=SimpleNamespace(bpm=120,meter=4,slot=lambda b:None)
        comp=mapper.Composer(song,[],0,16)
        comp.approach_by_difficulty=dict(NORMAL=1400,EXPERT=1600)
        self.assertEqual([comp.cfg(d)['approach_ms'] for d in mapper.DIFF],[1500,1400,1500,1600])
        self.assertEqual(mapper.parse_approach('expert=1600'),('EXPERT',1600))
        for bad in ('EXPERT=50','EXPERT=5001','HERO=1500','EASY=no'):
            with self.assertRaises(mapper.argparse.ArgumentTypeError):mapper.parse_approach(bad)

    def test_longer_travel_removes_overlapping_same_lane_notes(self):
        notes=[(0.,'CROSS','TAP',None),(.6,'CROSS','TAP',None)]
        self.assertEqual(len(mapper.fix(list(notes),120,1500)),2)
        self.assertEqual(len(mapper.fix(list(notes),120,1700)),1)

if __name__=='__main__':unittest.main()
