#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
using namespace std;

#include <fstream>
#include <climits>

/**
 * 【入力内容】
 * N：野生のポケモンの数
 *
 * 1匹目：	H[1]（体力）
 *			C[1]（捕獲可能HP）
 *			V[1]（経験値）
 *			A[1][0] B[1][0]（1つ目の技の威力と回数）
 *			A[1][1] B[1][1]（2つ目の技の威力と回数）
 * ～
 * i匹目：	H[i]（体力）
 *			C[i]（捕獲可能HP）
 *			V[i]（経験値）
 *			A[i][0] B[i][0]（1つ目の技の威力と回数）
 *			A[i][1] B[i][1]（2つ目の技の威力と回数）
 *
 * 最初の手持ちのポケモン：	A[0][0] B[0][0]（1つ目の技の威力と回数）
 *							A[0][1] B[0][1]（2つ目の技の威力と回数）
 */

 /**
  * ポケモンの静的な情報を管理する構造体
  */
struct StaticPokemonData
{
	int maxHp = 0;					// 最大体力
	int captureHp = 0;				// 捕獲可能HP
	int exp = 0;					// 経験値
	int movePower[2] = { 0,0 };		// 技の威力
	int maxMoveCount[2] = { 0,0 };	// 技の最大回数
	int totalDamage = 0;			// そのポケモンが持つ全技の合計ダメージ
	int catchCost = 0;				// 捕まえるのに必要なダメージ（maxHp - captureHp）
	float catchEfficiency = 0.0f;	// 捕まえる効率（totalDamage / catchCost）
	float defeatEfficiency = 0.0f;	// 倒す効率（exp / maxHp）
	float score = 0.0f;				// ソート用スコア（defeatEfficiency - catchEfficiency）
};
vector<StaticPokemonData> g_baseData;

/**
 * ポケモンの現在の居場所を定義
 */
enum class Location : uint8_t
{
	WILD = 0,	// 野生
	HAND,		// 手持ち
	BOX,		// ボックス
	FAINTED		// ひんし
};

/**
 * ポケモンの動的な情報を管理する構造体
 */
struct DynamicPokemonData
{
	int currentHp = 0;						// 現在の体力
	int remainingMoveCount[2] = { 0,0 };	// 現在の技の残り回数
	Location location = Location::WILD;		// 現在の居場所
};

/**
 * @brief ポケモンの情報から、合計ダメージ・捕まえるコスト・捕まえる効率を事前に計算する関数
 * @details
 *	totalDamage    = 全技の合計ダメージ（捕まえた後に使えるリソース量）
 *	catchCost      = maxHp - captureHp（捕まえるのに必要なダメージ）
 *	catchEfficiency = totalDamage / catchCost（投資対効果：高いほど捕まえる優先度が高い）
 */
void InitializePrecalc(StaticPokemonData& p)
{
	// 全技の合計ダメージ
	p.totalDamage = (p.movePower[0] * p.maxMoveCount[0]) + (p.movePower[1] * p.maxMoveCount[1]);
	// 捕まえるのに必要なダメージ
	p.catchCost = p.maxHp - p.captureHp;
	// 捕まえる効率（0除算を避けるため catchCost が 0 の場合は大きな値にする）
	if (p.catchCost > 0) {
		p.catchEfficiency = static_cast<float>(p.totalDamage) / p.catchCost;
	}
	else {
		p.catchEfficiency = static_cast<float>(p.totalDamage);
	}

	// 倒す効率
	if (p.maxHp > 0) {
		p.defeatEfficiency = static_cast<float>(p.exp) / p.maxHp;
	}
	else {
		p.defeatEfficiency = 0.0f;
	}

	// ソート用スコア（大きいほど「倒す優先」、小さいほど「捕まえる優先」）
	p.score = p.defeatEfficiency - p.catchEfficiency;
}

/**
 * @brief 手持ちの技の合計残りダメージを計算する関数
 */
int CalcTotalRemainingDamage(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	int total = 0;

	for (int h : handIds) {
		for (int m = 0; m < 2; ++m) {
			total += g_baseData[h].movePower[m] * pokemons[h].remainingMoveCount[m];
		}
	}
	return total;
}

/**
 * @brief  手持ちポケモン全員の技から、倒すために最大ダメージの技を選ぶ関数
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestAttackMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	// 最大ダメージ
	int bestDamage = -1;
	// 最適なアタッカーポケモンのインデックス
	int bestAttacker = -1;
	// 最適な技のインデックス
	int bestMove = -1;

	for (int handId : handIds) {
		for (int m = 0; m < 2; ++m) {
			if (pokemons[handId].remainingMoveCount[m] > 0 && g_baseData[handId].movePower[m] > bestDamage)
			{
				bestDamage = g_baseData[handId].movePower[m];
				bestAttacker = handId;
				bestMove = m;
			}
		}
	}

	return { bestAttacker, bestMove };
}

/**
 * @brief 手持ちポケモン全員の技から、捕まえるために最善の技を選ぶ関数
 * @details
 * 優先1：撃った後のHPが 1 以上 captureHp 以下に収まる技（captureHpに最も近いもの）
 * 優先2：優先1がなければ、captureHp より大きく残す技（最大ダメージ）
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestCatchMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int targetId)
{
	int currentHp = pokemons[targetId].currentHp;
	int captureHp = g_baseData[targetId].captureHp;

	// 優先1：1発で captureHp 以下に収める技（hpAfter が captureHp に最も近い）
	int best1Attacker = -1, best1Move = -1, best1HpAfter = -1;

	// 優先2：captureHp より大きいまま残す技（最大ダメージ）
	int best2Attacker = -1, best2Move = -1, best2Damage = -1;

	for (int handId : handIds) {
		for (int m = 0; m < 2; ++m) {
			if (pokemons[handId].remainingMoveCount[m] > 0)
			{
				int damage = g_baseData[handId].movePower[m];
				int hpAfter = currentHp - damage;

				// 優先1：captureHp 以下に収まる（hpAfter が大きいほど良い）
				if (hpAfter >= 1 && hpAfter <= captureHp && hpAfter > best1HpAfter)
				{
					best1HpAfter = hpAfter;
					best1Attacker = handId;
					best1Move = m;
				}
				// 優先2：まだ captureHp より大きいが、できるだけ削る
				else if (hpAfter > captureHp && damage > best2Damage)
				{
					best2Damage = damage;
					best2Attacker = handId;
					best2Move = m;
				}
			}
		}
	}

	if (best1Attacker != -1) return { best1Attacker, best1Move };
	if (best2Attacker != -1) return { best2Attacker, best2Move };
	return { -1, -1 };
}

/**
 * 手持ちに技が残っているかどうかを確認する関数
 */
bool HasAnyMove(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons)
{
	for (int handId : handIds) {
		if (pokemons[handId].remainingMoveCount[0] > 0 ||
			pokemons[handId].remainingMoveCount[1] > 0)
		{
			return true;
		}
	}
	return false;
}

/**
 * @brief 捕まえる効率が最も高い捕獲対象を選ぶ関数
 * @details
 *	捕まえる効率 = totalDamage / catchCost の降順で候補を評価する
 *	今の手持ちの合計残りダメージで捕まえられるものだけを対象にする
 * @return 捕獲対象の id。候補がなければ -1
 */
int SelectBestCatchTarget(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int N)
{
	int totalRemDmg = CalcTotalRemainingDamage(handIds, pokemons);

	int bestTarget = -1;
	float bestEfficiency = -1.0f;

	for (int i = 1; i <= N; i++)
	{
		// 野生のポケモン以外はスキップ
		if (pokemons[i].location != Location::WILD) continue;

		// 今の手持ちで捕まえるのに必要なダメージが賄えるか確認
		int requiredDmg = pokemons[i].currentHp - g_baseData[i].captureHp;
		if (totalRemDmg < requiredDmg) continue;

		// 捕まえる技が存在するか確認
		// （currentHp がすでに captureHp 以下なら攻撃不要で捕まえられるが、
		//   Step1 で捕獲済みのはずなのでここでは currentHp > captureHp のはず）
		auto [attackerId, moveIndex] = SelectBestCatchMove(handIds, pokemons, i);
		if (attackerId == -1) continue;

		// 捕まえる効率が最も高いものを選ぶ
		if (g_baseData[i].catchEfficiency > bestEfficiency)
		{
			bestEfficiency = g_baseData[i].catchEfficiency;
			bestTarget = i;
		}
	}

	return bestTarget;
}

/**
 * @brief 「経験値 / 現在HP」が最大の野生ポケモンを選ぶ関数（ひんし狙い）
 * @details
 *	残りHPが少ないほど、1ダメージあたりの期待経験値が高い。
 *	exp / currentHp を最大化することで、ダメージリソースの経験値効率を最大化する。
 * @return 攻撃対象の id。候補がなければ -1
 */
int SelectBestFaintTarget(
	const vector<DynamicPokemonData>& pokemons,
	int N)
{
	int bestTarget = -1;
	float bestEfficiency = -1.0f;

	for (int i = 1; i <= N; i++)
	{
		if (pokemons[i].location != Location::WILD) continue;
		// 経験値 / 現在HP（ダメージ1あたりの期待EXP）
		float eff = static_cast<float>(g_baseData[i].exp) / pokemons[i].currentHp;
		if (eff > bestEfficiency)
		{
			bestEfficiency = eff;
			bestTarget = i;
		}
	}

	return bestTarget;
}


/**
 * @brief 境界線kを決定する関数
 * @details
 *	ソート済みリストで先頭～k番目を「倒す対象」、k+1番目～末尾を「捕まえる対象」とする。
 *	必要ダメージ = 倒す対象のH合計 + 捕まえる対象の(H - C)合計
 *	与えられるダメージ = 初期ポケモンの技合計 + 捕まえる対象の技合計
 *	k = N-1（全員倒す）から始めて、条件を満たさなければkを減らす（捕まえる対象を増やす）。
 *	「与えられるダメージ >= 必要ダメージ」を満たす最大のk（倒す対象が最多）を返す。
 * @param sortedRank スコア降順にソートされたポケモンインデックス列
 * @return 境界線k（sortedRank上のインデックス。k番目まで倒す、k+1番目以降捕まえる）
 */
int DecideBoundary(const vector<int>& sortedRank)
{
	int N = (int)sortedRank.size();

	// 最初の手持ちの技の合計ダメージ
	int initialDamage = g_baseData[0].totalDamage;

	// 倒す対象のH合計と、捕まえる対象の(H - C)合計を初期化
	int defeatRequired = 0;
	// k = N-1（全員倒す）の状態で初期化
	for (int i = 0; i < N; i++) {
		defeatRequired += g_baseData[sortedRank[i]].maxHp;
	}

	// 捕まえる対象の(H - C)合計を初期化
	int catchRequired = 0;
	// 捕まえる対象の技の合計ダメージを初期化
	int catchDamage = 0;

	// k = N-1 から k = -1 に向かって探索
	// 条件を満たす最初のk（倒す対象が最多）を返す
	for (int k = N - 1; k >= -1; k--)
	{
		// 与えられるダメージ
		int givenDamage = initialDamage + catchDamage;
		// 必要ダメージ
		int neededDamage = defeatRequired + catchRequired;

		// 与えられるダメージが必要ダメージを上回るなら、kを返す
		if (givenDamage >= neededDamage) {
			return k;
		}

		// kを一つ減らす（k番目を捕まえる対象に移す）
		if (k >= 0)
		{
			int id = sortedRank[k];
			defeatRequired -= g_baseData[id].maxHp;
			catchRequired += g_baseData[id].maxHp - g_baseData[id].captureHp;
			catchDamage += g_baseData[id].totalDamage;
		}
	}
}


/******************************************************/


int main()
{
	// 提出時、以下はコメントアウト
	/** ここから */
	for (int fileIdx = 0; fileIdx <= 4; fileIdx++)
	{
		char inputPath[64];
		char outputPath[64];
		sprintf(inputPath, "入力ファイル/%04d.txt", fileIdx);
		sprintf(outputPath, "出力ファイル/%04d.out", fileIdx);
		ifstream is(inputPath);
		ofstream os(outputPath);
		// cin/cout のバッファを差し替える
		streambuf* oldIn = cin.rdbuf(is.rdbuf());
		streambuf* oldOut = cout.rdbuf(os.rdbuf());
		/** ここまで */

		////////////////////////////////////////////////////
		// 変数の宣言や、入力の受け取りなどを行うフェーズ //
		////////////////////////////////////////////////////

		// 野生のポケモンの数を読み込み
		int N;
		cin >> N;

		// 静的データ（g_baseData）のメモリ確保
		// 0番：最初の手持ち、1～N番：野生のポケモン
		g_baseData.assign(N + 1, StaticPokemonData());

		// 野生のポケモンの情報を読み込み
		for (int i = 1; i <= N; i++)
		{
			StaticPokemonData& p = g_baseData[i];
			cin >> p.maxHp >> p.captureHp >> p.exp
				>> p.movePower[0] >> p.maxMoveCount[0] >> p.movePower[1] >> p.maxMoveCount[1];

			// 合計ダメージ・捕まえるコスト・効率の事前計算
			InitializePrecalc(p);
		}

		// 最初の手持ちポケモンの情報を読み込み
		StaticPokemonData& p0 = g_baseData[0];
		cin >> p0.movePower[0] >> p0.maxMoveCount[0] >> p0.movePower[1] >> p0.maxMoveCount[1];
		InitializePrecalc(p0);

		// スコア（defeatEfficiency - catchEfficiency）の降順でソートしたインデックス列を作成
		// 先頭が「倒す優先」、末尾が「捕まえる優先」
		vector<int> sortedRank;
		for (int i = 1; i <= N; i++) {
			sortedRank.push_back(i);
		}
		sort(sortedRank.begin(), sortedRank.end(), [](int a, int b) {
			return g_baseData[a].score > g_baseData[b].score;
			});

		// 境界線kを決定する
		// sortedRank[0]～sortedRank[k]：倒す対象
		// sortedRank[k+1]～sortedRank[N-1]：捕まえる対象（技を補充するために順次捕まえる）
		int boundary = DecideBoundary(sortedRank);


		/////////////////////////////
		// 初期状態の構築           //
		/////////////////////////////

		vector<DynamicPokemonData> pokemons(N + 1);
		for (int i = 0; i <= N; i++)
		{
			pokemons[i].currentHp = g_baseData[i].maxHp;
			pokemons[i].remainingMoveCount[0] = g_baseData[i].maxMoveCount[0];
			pokemons[i].remainingMoveCount[1] = g_baseData[i].maxMoveCount[1];
			pokemons[i].location = (i == 0) ? Location::HAND : Location::WILD;
		}

		// 手持ちリスト（インデックスのみ）
		vector<int> handIds = { 0 };


		////////////////////
		// メインループ   //
		////////////////////

		while (true)
		{
			//=============================================================================
			// ステップ1：捕まえられる状態のポケモンを即捕獲（ボックス送り前に優先）
			//=============================================================================
			// 前のターンで削ったポケモンが捕まえられる状態になっている場合、
			// ボックス送りより先に捕獲することで技切れによる手持ち消失を防ぐ
			if ((int)handIds.size() < 6)
			{
				// 捕まえたかどうかのフラグ
				bool caught = false;

				// 捕まえられる状態のポケモンを探す（捕まえる効率が最も高いものを選ぶ）
				int bestCatchable = -1;
				float bestEfficiency = -1.0f;
				for (int i = 1; i <= N; i++)
				{
					if (pokemons[i].location != Location::WILD) continue;
					int currentHp = pokemons[i].currentHp;
					int captureHp = g_baseData[i].captureHp;
					if (currentHp >= 1 && currentHp <= captureHp)
					{
						if (g_baseData[i].catchEfficiency > bestEfficiency)
						{
							bestEfficiency = g_baseData[i].catchEfficiency;
							bestCatchable = i;
						}
					}
				}

				if (bestCatchable != -1)
				{
					// 捕まえられるので、手持ちに加える
					pokemons[bestCatchable].location = Location::HAND;
					handIds.push_back(bestCatchable);
					// 捕まえたポケモンの技の残り回数は最大に回復する
					pokemons[bestCatchable].remainingMoveCount[0] = g_baseData[bestCatchable].maxMoveCount[0];
					pokemons[bestCatchable].remainingMoveCount[1] = g_baseData[bestCatchable].maxMoveCount[1];
					// 捕まえる行動の出力
					cout << 2 << " " << bestCatchable << "\n";
					caught = true;
				}

				// 捕まえたポケモンがいれば、ボックス送りのステップはスキップして次のターンへ
				if (caught) continue;
			}

			//=============================================================================
			// ステップ2：技が尽きた手持ちをボックスへ送る
			//=============================================================================
			// ただし手持ちが 1 匹のときはボックスに送らない（詰み防止）
			if ((int)handIds.size() > 1)
			{
				vector<int> newHandIds;
				for (int h : handIds)
				{
					if (pokemons[h].remainingMoveCount[0] == 0 &&
						pokemons[h].remainingMoveCount[1] == 0)
					{
						pokemons[h].location = Location::BOX;
						cout << 3 << " " << h << "\n";
					}
					else
					{
						newHandIds.push_back(h);
					}
				}
				handIds = newHandIds;
			}
			// 手持ちの技が全て尽きたらループを抜ける
			if (!HasAnyMove(handIds, pokemons)) break;


			//=============================================================================
			// ステップ3：技を使う行動
			//=============================================================================
			// 行動したかどうかのフラグ
			bool acted = false;

			// 手持ちが 6 匹未満：捕まえることを優先
			if ((int)handIds.size() < 6)
			{
				// 捕まえる効率が最も高い捕獲対象を探す
				int catchTarget = SelectBestCatchTarget(handIds, pokemons, N);

				if (catchTarget != -1)
				{
					// 捕獲狙いの技を選んで攻撃
					auto [attackerId, moveIndex] = SelectBestCatchMove(handIds, pokemons, catchTarget);
					if (attackerId != -1)
					{
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[catchTarget].currentHp -= damage;
						pokemons[attackerId].remainingMoveCount[moveIndex]--;
						if (pokemons[catchTarget].currentHp <= 0)
							pokemons[catchTarget].location = Location::FAINTED;
						cout << 1 << " " << attackerId << " " << catchTarget << " " << (moveIndex + 1) << "\n";
						acted = true;
					}
				}

				// 捕まえる対象がなければ、残りHPが最小の野生ポケモンをひんし狙いで攻撃
				if (!acted)
				{
					int faintTarget = SelectBestFaintTarget(pokemons, N);
					if (faintTarget != -1)
					{
						auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
						if (attackerId != -1)
						{
							int damage = g_baseData[attackerId].movePower[moveIndex];
							pokemons[faintTarget].currentHp -= damage;
							pokemons[attackerId].remainingMoveCount[moveIndex]--;
							if (pokemons[faintTarget].currentHp <= 0)
								pokemons[faintTarget].location = Location::FAINTED;
							cout << 1 << " " << attackerId << " " << faintTarget << " " << (moveIndex + 1) << "\n";
							acted = true;
						}
					}
				}
			}
			// 手持ちが 6 匹：残りHPが最小の野生ポケモンをひんし狙いで攻撃
			else
			{
				int faintTarget = SelectBestFaintTarget(pokemons, N);
				if (faintTarget != -1)
				{
					auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
					if (attackerId != -1)
					{
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[faintTarget].currentHp -= damage;
						pokemons[attackerId].remainingMoveCount[moveIndex]--;
						if (pokemons[faintTarget].currentHp <= 0)
							pokemons[faintTarget].location = Location::FAINTED;
						cout << 1 << " " << attackerId << " " << faintTarget << " " << (moveIndex + 1) << "\n";
						acted = true;
					}
				}
			}

			if (!acted) break;
		}

		// 提出時、以下はコメントアウト
		/** ここから */
		cin.rdbuf(oldIn);
		cout.rdbuf(oldOut);
	}
	/** ここまで */

	return 0;
}