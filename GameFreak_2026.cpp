#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
using namespace std;

#include <fstream>

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
	float score = 0.0f;				// 最終スコア（倒すスコア - 捕まえるスコア）
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
 * @brief ポケモンの情報から、全技の合計ダメージとスコアを事前に計算しておく関数
 * @details
 *	倒すスコア     = exp / maxHp
 *	捕まえるスコア = totalDamage / requiredHp  (requiredHp = maxHp - captureHp)
 *	最終スコア     = 倒すスコア - 捕まえるスコア
 *	スコアが大きいほど「倒す優先」、小さいほど「捕まえる優先」
 */
void InitializePrecalc(StaticPokemonData& p)
{
	// 全技の合計ダメージ
	p.totalDamage = (p.movePower[0] * p.maxMoveCount[0]) + (p.movePower[1] * p.maxMoveCount[1]);
	// 倒すスコア（正規化：最大 5000/500=10.0）
	float defeatScore = static_cast<float>(p.exp) / p.maxHp / 10.0f;
	// 捕まえるスコア（正規化：最大 3750-375=3375）
	// totalDamage - catchCost が大きいほど捕まえる価値が高い
	float catchScore = static_cast<float>(p.totalDamage - (p.maxHp - p.captureHp)) / 3375.0f;
	// 最終スコア（大きいほど「倒す優先」、小さいほど「捕まえる優先」）
	p.score = defeatScore - catchScore;
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
 * @brief 指定した技リストで対象をCまで削り切れるか判定する関数
 * @details
 *	各技を任意の回数・順序で使った場合に、HPが1以上captureHp以下に収まる状態に
 *	到達できるかをDPで判定する。
 * @param currentHp  現在のHP
 * @param captureHp  捕獲可能HP（C）
 * @param moves      使用可能な技リスト {威力, 残り回数}
 * @return 削り切れる場合true、不可能な場合false
 */
bool CanReduceToCaptureRangeDP(
	int currentHp,
	int captureHp,
	const vector<pair<int, int>>& moves)
{
	// すでにC以下なら削る必要なし
	if (currentHp >= 1 && currentHp <= captureHp) return true;

	// DPで到達可能なHP集合を管理
	vector<bool> reachable(currentHp + 1, false);
	reachable[currentHp] = true;

	for (auto& [power, count] : moves)
	{
		for (int k = 0; k < count; ++k)
		{
			vector<bool> next = reachable;
			for (int hp = 1; hp <= currentHp; ++hp)
			{
				if (!reachable[hp]) continue;
				int newHp = hp - power;
				if (newHp >= 1 && newHp <= currentHp)
					next[newHp] = true;
			}
			reachable = next;
		}
	}

	for (int hp = 1; hp <= captureHp; ++hp)
	{
		if (reachable[hp]) return true;
	}
	return false;
}

/**
 * @brief 手持ちの技の組み合わせで対象をCまで削り切れるか判定する関数
 * @return 削り切れる場合true、不可能な場合false
 */
bool CanReduceToCaptureRange(
	const vector<int>& handIds,
	const vector<DynamicPokemonData>& pokemons,
	int targetId)
{
	int currentHp = pokemons[targetId].currentHp;
	int captureHp = g_baseData[targetId].captureHp;

	vector<pair<int, int>> moves;
	for (int h : handIds) {
		for (int m = 0; m < 2; ++m) {
			if (pokemons[h].remainingMoveCount[m] > 0) {
				moves.push_back({ g_baseData[h].movePower[m], pokemons[h].remainingMoveCount[m] });
			}
		}
	}

	return CanReduceToCaptureRangeDP(currentHp, captureHp, moves);
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
				// ただし撃った後の残り回数でCまで削り切れる場合のみ
				else if (hpAfter > captureHp && damage > best2Damage)
				{
					// 撃った後の残り回数で削り切れるか確認
					vector<pair<int, int>> movesAfter;
					for (int hh : handIds) {
						for (int mm = 0; mm < 2; ++mm) {
							int rem = pokemons[hh].remainingMoveCount[mm];
							if (hh == handId && mm == m) rem--; // この技を撃った後
							if (rem > 0)
								movesAfter.push_back({ g_baseData[hh].movePower[mm], rem });
						}
					}
					if (CanReduceToCaptureRangeDP(hpAfter, captureHp, movesAfter))
					{
						best2Damage = damage;
						best2Attacker = handId;
						best2Move = m;
					}
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

			// スコア計算用の事前処理
			InitializePrecalc(p);
		}

		// 最初の手持ちポケモンの情報を読み込み
		StaticPokemonData& p0 = g_baseData[0];
		cin >> p0.movePower[0] >> p0.maxMoveCount[0] >> p0.movePower[1] >> p0.maxMoveCount[1];
		InitializePrecalc(p0);

		// スコアの降順でソートしたインデックス列を作成
		// 先頭が「倒す優先」、末尾が「捕まえる優先」
		vector<int> sortedRank;
		for (int i = 1; i <= N; i++) {
			sortedRank.push_back(i);
		}
		sort(sortedRank.begin(), sortedRank.end(), [](int a, int b) {
			return g_baseData[a].score > g_baseData[b].score;
			});


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
			// ステップ1：捕まえられるポケモンを末尾から探して即捕獲（ボックス送り前に優先）
			//=============================================================================
			// 前のターンで削ったポケモンが捕まえられる状態になっている場合、
			// ボックス送りより先に捕獲することで技切れによる手持ち消失を防ぐ
			if ((int)handIds.size() < 6)
			{
				// 捕まえたかどうかのフラグ
				bool caught = false;

				// 末尾から捕まえられるポケモンを探す
				for (int ri = (int)sortedRank.size() - 1; ri >= 0; --ri)
				{
					int targetId = sortedRank[ri];

					// 野生のポケモン以外はスキップ
					if (pokemons[targetId].location != Location::WILD) continue;

					// 捕まえられるかどうかを確認
					int currentHp = pokemons[targetId].currentHp;
					int captureHp = g_baseData[targetId].captureHp;
					if (currentHp >= 1 && currentHp <= captureHp)
					{
						// 捕まえられるので、手持ちに加える
						pokemons[targetId].location = Location::HAND;
						handIds.push_back(targetId);
						// 捕まえたポケモンの技の残り回数は最大に回復する
						pokemons[targetId].remainingMoveCount[0] = g_baseData[targetId].maxMoveCount[0];
						pokemons[targetId].remainingMoveCount[1] = g_baseData[targetId].maxMoveCount[1];
						// 捕まえる行動の出力
						cout << 2 << " " << targetId << "\n";
						caught = true;
						break;
					}
				}
				// 捕まえたポケモンがいれば、ボックス送りのステップはスキップして次のターンへ
				if (caught) continue;
			}

			//=============================================================================
			// ステップ2：技が尽きた手持ちをボックスへ送る
			//=============================================================================
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
			// 手持ちが空になったらゲームオーバーなのでループを抜ける
			if (!HasAnyMove(handIds, pokemons)) break;


			//=============================================================================
			// ステップ3：技を使う行動
			//=============================================================================
			// 行動したかどうかのフラグ
			bool acted = false;

			// 手持ちが 6 匹未満：捕まえることを優先
			if ((int)handIds.size() < 6)
			{
				// 手持ちの合計残りダメージを計算
				int totalRemDmg = CalcTotalRemainingDamage(handIds, pokemons);

				// 末尾から「今の手持ちで捕まえ可能」なポケモンを探す
				for (int ri = (int)sortedRank.size() - 1; ri >= 0; --ri)
				{
					int targetId = sortedRank[ri];

					// 野生のポケモン以外はスキップ
					if (pokemons[targetId].location != Location::WILD) continue;

					// 手持ちの技の組み合わせで実際にCまで削り切れるか確認
					// 合計ダメージが足りていても技の刻み方でCを飛び越える場合があるため
					if (!CanReduceToCaptureRange(handIds, pokemons, targetId)) continue;

					// 削れる技を選ぶ
					auto [attackerId, moveIndex] = SelectBestCatchMove(handIds, pokemons, targetId);
					if (attackerId != -1)
					{
						// 技を撃ってダメージを与える
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[targetId].currentHp -= damage;
						// 手持ちの技の残り回数を減らす
						pokemons[attackerId].remainingMoveCount[moveIndex]--;
						// ダメージを与えた結果、倒れていればひんしにする
						if (pokemons[targetId].currentHp <= 0) {
							pokemons[targetId].location = Location::FAINTED;
						}
						// 行動の出力
						cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
						// 技を撃ったのでループを抜けて次のターンへ
						acted = true;
						break;
					}
				}

				// 捕まえる対象がなければ倒す対象（先頭）を攻撃
				if (!acted)
				{
					for (int targetId : sortedRank)
					{
						if (pokemons[targetId].location != Location::WILD) continue;
						auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
						if (attackerId != -1)
						{
							int damage = g_baseData[attackerId].movePower[moveIndex];
							pokemons[targetId].currentHp -= damage;
							pokemons[attackerId].remainingMoveCount[moveIndex]--;
							if (pokemons[targetId].currentHp <= 0)
								pokemons[targetId].location = Location::FAINTED;
							cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
							acted = true;
						}
						break;
					}
				}
			}
			// 手持ちが 6 匹：倒すことに専念
			else
			{
				for (int targetId : sortedRank)
				{
					if (pokemons[targetId].location != Location::WILD) continue;
					auto [attackerId, moveIndex] = SelectBestAttackMove(handIds, pokemons);
					if (attackerId != -1)
					{
						int damage = g_baseData[attackerId].movePower[moveIndex];
						pokemons[targetId].currentHp -= damage;
						pokemons[attackerId].remainingMoveCount[moveIndex]--;
						if (pokemons[targetId].currentHp <= 0)
							pokemons[targetId].location = Location::FAINTED;
						cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
						acted = true;
					}
					break;
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